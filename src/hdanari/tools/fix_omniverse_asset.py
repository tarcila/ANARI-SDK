#!/usr/bin/env python3
# Copyright 2025-2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Sidecar fix-ups for Omniverse-authored USD assets so they render correctly
with hdanari (and other UsdLux-standard Hydra delegates).

Omniverse/Kit assets carry a few conventions that don't survive a round-trip
through standard UsdImaging:

  1. Legacy UsdLux light encoding. Lights author bare attributes
     (``intensity``, ``texture:file``, ``radius`` ...) instead of the
     ``inputs:``-namespaced ones UsdImaging reads. The bare values are then
     silently ignored -- e.g. a dome's HDRI never loads.

  2. Material bindings authored without the ``MaterialBindingAPI`` applied
     schema. Newer Hydra scene-index paths skip such bindings.

  3. Dome-light coordinate convention. Omniverse authors domes in a +Z-up /
     +X-forward frame; OpenUSD is +Y-up / +Z-forward. The HDRI environment is
     therefore rotated when rendered by a USD-standard delegate. We prepend the
     fixed basis-change rotation to each dome's xformOpOrder, preserving any
     rotation the asset itself authored. (Skip with ``--no-dome-reorient`` if
     the stage already declares ``upAxis = "Z"``, which the delegate handles,
     to avoid double-compensating.)

  4. Exposure. Kit balances a bright dome against the camera's photographic
     exposure (fStop/shutter/ISO) and a tonemapper. A renderer that applies
     neither blows out; one that applies only camera exposure (as hdanari now
     does) renders the dimmed dome black. Until hdanari handles the full
     pipeline we sidestep it: clamp the dome intensity to a face value *and*
     neutralize the camera's photographic exposure so neither darkens nor
     brightens the result.

  5. Windows-style asset paths. Kit authors texture references with backslash
     separators (``..\\Materials\\Textures\\foo.png``) that USD's resolver
     cannot open on non-Windows platforms, so the texture silently fails to
     load. We normalize them and re-anchor to a resolved absolute path.

  6. Stage up axis (opt-in, ``--up-axis``). Omniverse stages are typically Z-up
     but may not author ``upAxis`` metadata; declaring it lets the delegate
     orient ``poleAxis="scene"`` domes and the host orient its camera. Pair
     ``--up-axis Z`` with ``--no-dome-reorient`` so the dome isn't compensated
     twice.

  7. Doubled MaterialX ``fileprefix``. usdMtlx applies a MaterialX
     ``fileprefix`` that some assets also baked into the file value (a
     workaround for older usdMtlx that ignored it), so the path arrives doubled
     (``./textures/./textures/foo.png``) and fails to resolve. The render
     delegate cannot fix this -- usdMtlx drops the source-layer anchor, so a
     relative path can no longer be re-resolved downstream. Here we still have
     the authoring layer, so we collapse the duplicated run and re-anchor to a
     resolved absolute path.

Omniverse assets instance heavily, and the prims needing most of these fixes
(meshes, their bindings, their textures) live inside instance prototypes. A
plain ``stage.Traverse()`` stops at the instance boundary, so every pass would
silently touch zero prims. Instead we traverse instance proxies to *find* the
work, then de-instance only the ancestors that gate a needed fix (preserving
instancing everywhere else) so the corrections can be authored. Pass
``--no-deinstance`` to keep all instancing and skip fixes on instanced geometry.

The script never edits the input: it writes a thin *overlay* layer that
sublayers the original and authors only the corrections, so it is
non-destructive and easy to discard. Render the overlay instead of the
original::

    fix_omniverse_asset.py Clock.usda            # -> Clock.hdanari.usda
    usdrecord -r Anari Clock.hdanari.usda out.png
"""

import argparse
import os
import sys

from pxr import Gf, Sdf, Usd, UsdGeom, UsdLux, UsdShade, Vt

# Basis change from Omniverse's dome convention (+Z up, +X forward) to OpenUSD's
# (+Y up, +Z forward), as rotateXYZ degrees: Rz(-90)*Ry(-90) maps +Z->+Y and
# +X->+Z. Prepended to each dome's authored xformOps, not replacing them.
DEFAULT_DOME_REORIENT_XYZ = Gf.Vec3d(0.0, -90.0, -90.0)

# Suffix marking the op we author, so reruns update it instead of stacking.
DOME_REORIENT_OP = "xformOp:rotateXYZ:omniReorient"

# Dome intensity to clamp to (workaround until hdanari honors exposure).
DEFAULT_DOME_INTENSITY = 1.0

# Camera exposure attributes (Kit uses the colon-namespaced spellings) reset to
# values that make UsdGeomCamera's linear exposure scale 1.0 (no-op).
NEUTRAL_CAMERA_EXPOSURE = {
    "exposure": 0.0,
    "exposureFStop": 1.0,
    "exposure:fStop": 1.0,
    "exposureTime": 1.0,
    "exposure:time": 1.0,
    "exposureResponsivity": 1.0,
    "exposure:responsivity": 1.0,
    "exposureIso": 100.0,
    "exposure:iso": 100.0,
}


def _is_light(prim):
    return prim.HasAPI(UsdLux.LightAPI) or prim.GetTypeName().endswith("Light")


def _is_dome_light(prim):
    return prim.IsA(UsdLux.DomeLight) or prim.GetTypeName() == "DomeLight"


def _is_camera(prim):
    return prim.IsA(UsdGeom.Camera) or prim.GetTypeName() == "Camera"


def _iter_prims(stage):
    """Traverse including instance proxies.

    Omniverse assets instance heavily (e.g. every window references a shared
    ``window.usd``), so the prims that need fixing -- meshes, their material
    bindings, textures -- live inside prototypes. A plain ``stage.Traverse()``
    stops at the instance boundary and never sees them, so a fix-up that uses it
    silently touches zero prims. Instance proxies are read-only, though, so a
    pass must still de-instance the gating ancestor (see ``deinstance_for_fixes``)
    before it can *author* on a matched proxy."""
    return iter(Usd.PrimRange(stage.GetPseudoRoot(), Usd.TraverseInstanceProxies()))


def _has_material_binding(prim):
    return any(
        rel.HasAuthoredTargets()
        and (
            rel.GetName() == "material:binding"
            or rel.GetName().startswith("material:binding:")
        )
        for rel in prim.GetRelationships()
    )


def _needs_binding_api(prim):
    return _has_material_binding(prim) and not prim.HasAPI(
        UsdShade.MaterialBindingAPI
    )


def _needs_light_migration(prim):
    if not _is_light(prim):
        return False
    for attr in prim.GetAttributes():
        name = attr.GetName()
        if name.startswith("inputs:") or not attr.HasAuthoredValue():
            continue
        input_attr = prim.GetAttribute("inputs:" + name)
        if input_attr and input_attr.IsDefined() and not input_attr.HasAuthoredValue():
            return True
    return False


def _needs_camera_exposure(prim):
    return _is_camera(prim) and any(
        prim.GetAttribute(name) and prim.GetAttribute(name).HasAuthoredValue()
        for name in NEUTRAL_CAMERA_EXPOSURE
    )


def _has_asset_matching(prim, predicate):
    for attr in prim.GetAttributes():
        type_name = attr.GetTypeName()
        if type_name == Sdf.ValueTypeNames.Asset:
            if predicate(attr.Get()):
                return True
        elif type_name == Sdf.ValueTypeNames.AssetArray:
            if any(predicate(item) for item in attr.Get() or []):
                return True
    return False


def _has_backslash_asset(prim):
    return _has_asset_matching(
        prim, lambda v: isinstance(v, Sdf.AssetPath) and "\\" in v.path
    )


def _has_dedupable_asset(prim):
    return _has_asset_matching(
        prim,
        lambda v: isinstance(v, Sdf.AssetPath)
        and not v.resolvedPath
        and _collapse_duplicated_prefix(v.path) != v.path,
    )


def deinstance_for_fixes(stage, predicate):
    """De-instance the minimal set of instanceable ancestors so every proxy the
    authoring passes need to touch becomes a real, editable prim.

    ``predicate(prim)`` reports whether a proxy would be fixed by an enabled
    pass. We only de-instance ancestors that gate such a prim, so instancing is
    preserved everywhere else. Iterates to a fixpoint: a nested instance only
    surfaces (and its own instanceable flag becomes visible) once the outer
    instance around it is de-instanced. Returns the set of de-instanced paths."""
    deinstanced = set()
    while True:
        gating = set()
        for prim in _iter_prims(stage):
            if not prim.IsInstanceProxy() or not predicate(prim):
                continue
            for prefix in prim.GetPath().GetPrefixes():
                ancestor = stage.GetPrimAtPath(prefix)
                if ancestor and ancestor.IsInstance() and prefix not in deinstanced:
                    gating.add(prefix)
        if not gating:
            return deinstanced
        for path in gating:
            stage.OverridePrim(path).SetInstanceable(False)
        deinstanced |= gating


def set_stage_up_axis(stage, axis):
    """Author the stage upAxis metadata on the overlay's edit target.

    Returns (previous, current) tokens for reporting."""
    token = UsdGeom.Tokens.z if axis == "Z" else UsdGeom.Tokens.y
    previous = UsdGeom.GetStageUpAxis(stage)
    UsdGeom.SetStageUpAxis(stage, token)
    return previous, token


def migrate_legacy_light_inputs(stage):
    """Copy bare light attributes into their ``inputs:`` counterparts."""
    migrated = []
    for prim in _iter_prims(stage):
        if prim.IsInstanceProxy() or not _is_light(prim):
            continue
        for attr in list(prim.GetAttributes()):
            name = attr.GetName()
            if name.startswith("inputs:") or not attr.HasAuthoredValue():
                continue
            input_attr = prim.GetAttribute("inputs:" + name)
            if (
                input_attr
                and input_attr.IsDefined()
                and not input_attr.HasAuthoredValue()
            ):
                value = attr.Get()
                # Asset paths are authored relative to the source layer; we
                # write into a sidecar overlay elsewhere, so re-anchor them to
                # the resolved absolute path or they stop resolving.
                if isinstance(value, Sdf.AssetPath) and value.resolvedPath:
                    value = Sdf.AssetPath(value.resolvedPath)
                input_attr.Set(value)
                migrated.append("%s.%s" % (prim.GetPath(), name))
    return migrated


def apply_material_binding_api(stage):
    """Apply MaterialBindingAPI wherever a material:binding is authored."""
    fixed = []
    for prim in _iter_prims(stage):
        if prim.IsInstanceProxy():
            continue
        if _needs_binding_api(prim):
            UsdShade.MaterialBindingAPI.Apply(prim)
            fixed.append(str(prim.GetPath()))
    return fixed


def reorient_dome_lights(stage, rotate_xyz, prepend=True):
    """Insert the Omniverse->USD basis-change rotation into each dome's
    xformOpOrder, preserving the asset's own ops.

    Prepending makes the basis change the innermost op (applied in the dome's
    local frame before its authored placement); append to apply it outermost
    instead. A unique op suffix keeps reruns idempotent."""
    fixed = []
    for prim in _iter_prims(stage):
        if prim.IsInstanceProxy() or not _is_dome_light(prim):
            continue
        prim.CreateAttribute(
            DOME_REORIENT_OP, Sdf.ValueTypeNames.Double3, False
        ).Set(rotate_xyz)
        order_attr = prim.GetAttribute("xformOpOrder")
        order = (
            list(order_attr.Get())
            if order_attr and order_attr.HasAuthoredValue()
            else []
        )
        order = [op for op in order if op != DOME_REORIENT_OP]
        order = (
            [DOME_REORIENT_OP] + order if prepend else order + [DOME_REORIENT_OP]
        )
        prim.CreateAttribute(
            "xformOpOrder", Sdf.ValueTypeNames.TokenArray, False
        ).Set(Vt.TokenArray(order))
        fixed.append(str(prim.GetPath()))
    return fixed


def dim_dome_intensity(stage, intensity):
    """Clamp dome-light intensity to a displayable value."""
    fixed = []
    for prim in _iter_prims(stage):
        if prim.IsInstanceProxy() or not _is_dome_light(prim):
            continue
        UsdLux.DomeLight(prim).CreateIntensityAttr().Set(intensity)
        fixed.append(str(prim.GetPath()))
    return fixed


def neutralize_camera_exposure(stage):
    """Reset authored camera photographic exposure so it neither darkens nor
    brightens, pairing with the dome-intensity clamp."""
    fixed = []
    for prim in _iter_prims(stage):
        if prim.IsInstanceProxy() or not _is_camera(prim):
            continue
        touched = False
        for name, value in NEUTRAL_CAMERA_EXPOSURE.items():
            attr = prim.GetAttribute(name)
            if attr and attr.HasAuthoredValue():
                attr.Set(float(value))
                touched = True
        if touched:
            fixed.append(str(prim.GetPath()))
    return fixed


def _resolve_asset(attr, rel_path):
    """Resolve a (normalized) asset path against the layers that author the
    attribute. Returns an absolute path that exists, or None."""
    if os.path.isabs(rel_path):
        return rel_path if os.path.exists(rel_path) else None
    for spec in attr.GetPropertyStack(Usd.TimeCode.Default()):
        layer = spec.layer
        if not layer or not layer.realPath:
            continue
        candidate = os.path.normpath(
            os.path.join(os.path.dirname(layer.realPath), rel_path)
        )
        if os.path.exists(candidate):
            return candidate
    return None


def _normalize_asset(attr, value):
    """Return a resolved Sdf.AssetPath for a backslash-authored path, else None."""
    if not isinstance(value, Sdf.AssetPath) or "\\" not in value.path:
        return None
    resolved = _resolve_asset(attr, value.path.replace("\\", "/"))
    return Sdf.AssetPath(resolved) if resolved else None


def _collapse_duplicated_prefix(path):
    """Collapse an immediately-repeated leading directory run:
    ``tex/./tex/foo.png`` -> ``tex/foo.png``. Returns ``path`` unchanged when
    there is no such duplication."""
    normalized = os.path.normpath(path)
    for k in range(len(normalized) // 2, 0, -1):
        if normalized[k - 1] == "/" and normalized[:k] == normalized[k : 2 * k]:
            return normalized[k:]
    return path


def _dedup_asset(attr, value):
    """Return a resolved Sdf.AssetPath for a doubled-``fileprefix`` path, else None.

    usdMtlx applies a MaterialX ``fileprefix`` that some assets also baked into
    the file value, so the path arrives doubled (``./tex/./tex/foo.png``) and
    fails to resolve. Unlike the render delegate we still have the authoring
    layer, so we can collapse the run and re-resolve against it."""
    if not isinstance(value, Sdf.AssetPath) or value.resolvedPath:
        return None  # unset or already resolves -> nothing to fix
    collapsed = _collapse_duplicated_prefix(value.path)
    if collapsed == value.path:
        return None
    resolved = _resolve_asset(attr, collapsed)
    return Sdf.AssetPath(resolved) if resolved else None


def _rewrite_asset_paths(stage, transform):
    """Apply ``transform(attr, value) -> Sdf.AssetPath|None`` to every scalar
    ``asset`` and array ``asset[]`` (e.g. UDIM tile sets) attribute, rewriting
    those it returns a value for. Returns the list of touched attribute paths."""
    fixed = []
    for prim in _iter_prims(stage):
        if prim.IsInstanceProxy():
            continue
        for attr in prim.GetAttributes():
            type_name = attr.GetTypeName()
            if type_name == Sdf.ValueTypeNames.Asset:
                rewritten = transform(attr, attr.Get())
                if rewritten is not None:
                    attr.Set(rewritten)
                    fixed.append("%s.%s" % (prim.GetPath(), attr.GetName()))
            elif type_name == Sdf.ValueTypeNames.AssetArray:
                items = attr.Get()
                if not items:
                    continue
                out = [transform(attr, item) for item in items]
                if any(r is not None for r in out):
                    attr.Set(
                        Sdf.AssetPathArray(
                            [r if r is not None else src for r, src in zip(out, items)]
                        )
                    )
                    fixed.append("%s.%s" % (prim.GetPath(), attr.GetName()))
    return fixed


def normalize_asset_paths(stage):
    """Rewrite Windows-style backslash asset paths to resolved absolute paths."""
    return _rewrite_asset_paths(stage, _normalize_asset)


def dedup_asset_prefixes(stage):
    """Rewrite doubled-``fileprefix`` MaterialX texture paths to resolved
    absolute paths (see ``_dedup_asset``)."""
    return _rewrite_asset_paths(stage, _dedup_asset)


def _make_overlay(input_path, output_path):
    if os.path.abspath(output_path) == os.path.abspath(input_path):
        sys.exit(
            "error: output '%s' must differ from input; the overlay would "
            "clobber the source" % output_path
        )

    input_layer = Sdf.Layer.FindOrOpen(input_path)
    if not input_layer:
        sys.exit("error: cannot open input layer '%s'" % input_path)

    try:
        out_layer = Sdf.Layer.CreateNew(output_path)
    except Exception as e:  # Tf errors: bad output dir, unknown extension, etc.
        sys.exit("error: cannot create overlay '%s': %s" % (output_path, e))
    sublayer = os.path.relpath(
        os.path.abspath(input_path), os.path.dirname(os.path.abspath(output_path))
    )
    out_layer.subLayerPaths = [sublayer]

    stage = Usd.Stage.Open(out_layer)
    stage.SetEditTarget(Usd.EditTarget(out_layer))
    return stage, out_layer


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("input", help="Omniverse-authored USD file")
    parser.add_argument(
        "-o", "--output", help="overlay layer to write (default: <input>.hdanari.usda)"
    )
    presets = parser.add_mutually_exclusive_group()
    presets.add_argument(
        "--omniverse",
        action="store_true",
        help="preset for Kit/Omniverse assets: --no-exposure "
        "--dome-reorient-append --dome-reorient 90 0 0",
    )
    presets.add_argument(
        "--omniverse-zup",
        action="store_true",
        help="like --omniverse, plus --up-axis Z (for stages that don't already "
        "declare it)",
    )
    parser.add_argument(
        "--dome-intensity",
        type=float,
        default=DEFAULT_DOME_INTENSITY,
        help="dome-light intensity to clamp to (default: %(default)s)",
    )
    parser.add_argument(
        "--up-axis",
        choices=["Y", "Z"],
        help="author the stage upAxis metadata on the overlay (default: leave "
        "the asset's value untouched). Pair --up-axis Z with --no-dome-reorient.",
    )
    parser.add_argument(
        "--dome-reorient",
        type=float,
        nargs=3,
        metavar=("RX", "RY", "RZ"),
        default=list(DEFAULT_DOME_REORIENT_XYZ),
        help="Omniverse->USD dome basis-change rotateXYZ degrees, prepended to "
        "the dome's xformOps (default: %(default)s)",
    )
    parser.add_argument(
        "--dome-reorient-append",
        action="store_true",
        help="append the dome reorientation as the outermost op instead of "
        "prepending it",
    )
    parser.add_argument(
        "--no-lights", action="store_true", help="skip legacy light input migration"
    )
    parser.add_argument(
        "--no-bindings", action="store_true", help="skip MaterialBindingAPI application"
    )
    parser.add_argument(
        "--no-dome-reorient",
        action="store_true",
        help="skip dome coordinate-convention reorientation (e.g. when the "
        "stage declares upAxis=Z, already handled by the delegate)",
    )
    parser.add_argument(
        "--no-exposure",
        action="store_true",
        help="skip exposure normalization (dome intensity clamp + camera "
        "exposure neutralization)",
    )
    parser.add_argument(
        "--no-texture-paths",
        action="store_true",
        help="skip Windows-style asset path normalization",
    )
    parser.add_argument(
        "--no-fileprefix-dedup",
        action="store_true",
        help="skip collapsing doubled MaterialX fileprefix texture paths",
    )
    parser.add_argument(
        "--no-deinstance",
        action="store_true",
        help="do not de-instance to reach fixes inside instance prototypes. "
        "Instance proxies are read-only, so any needed fix on instanced geometry "
        "(most Omniverse meshes and their material bindings) is skipped instead.",
    )
    args = parser.parse_args()

    # Convenience presets. The reorient here is a deliberate custom rotation
    # (not the default basis change), so the double-compensation warning below
    # does not apply -- suppress it for the presets.
    if args.omniverse or args.omniverse_zup:
        args.no_exposure = True
        args.dome_reorient_append = True
        args.dome_reorient = [90.0, 0.0, 0.0]
        if args.omniverse_zup:
            args.up_axis = "Z"

    if (
        args.up_axis == "Z"
        and not args.no_dome_reorient
        and not (args.omniverse or args.omniverse_zup)
    ):
        print(
            "warning: --up-axis Z with dome reorientation enabled "
            "double-compensates the HDRI; pass --no-dome-reorient",
            file=sys.stderr,
        )

    basename, ext = os.path.splitext(args.input)
    output = args.output or basename + ".hdanari" + ext
    stage, out_layer = _make_overlay(args.input, output)

    if args.up_axis:
        previous, current = set_stage_up_axis(stage, args.up_axis)
        print("set stage upAxis %s -> %s" % (previous, current))

    # The authoring passes below cannot edit instance proxies, so de-instance the
    # ancestors that gate any prim an enabled pass would touch. Build the union
    # predicate from the enabled passes so untouched instancing is preserved.
    if not args.no_deinstance:
        checks = []
        if not args.no_lights:
            checks.append(_needs_light_migration)
        if not args.no_bindings:
            checks.append(_needs_binding_api)
        if not args.no_dome_reorient or not args.no_exposure:
            checks.append(_is_dome_light)
        if not args.no_exposure:
            checks.append(_needs_camera_exposure)
        if not args.no_texture_paths:
            checks.append(_has_backslash_asset)
        if not args.no_fileprefix_dedup:
            checks.append(_has_dedupable_asset)
        if checks:
            deinstanced = deinstance_for_fixes(
                stage, lambda prim: any(check(prim) for check in checks)
            )
            print(
                "de-instanced %d prim(s) to expose fixes inside prototypes"
                % len(deinstanced)
            )

    # Lights first: migration creates inputs:intensity, the dome clamp then
    # overrides it with the displayable value.
    if not args.no_lights:
        m = migrate_legacy_light_inputs(stage)
        print("migrated %d legacy light input(s)" % len(m))
    if not args.no_bindings:
        b = apply_material_binding_api(stage)
        print("applied MaterialBindingAPI to %d prim(s)" % len(b))
    if not args.no_dome_reorient:
        r = reorient_dome_lights(
            stage, Gf.Vec3d(*args.dome_reorient), prepend=not args.dome_reorient_append
        )
        print(
            "reoriented %d dome light(s) by rotateXYZ=%s (%s)"
            % (
                len(r),
                args.dome_reorient,
                "appended" if args.dome_reorient_append else "prepended",
            )
        )
    if not args.no_exposure:
        i = dim_dome_intensity(stage, args.dome_intensity)
        c = neutralize_camera_exposure(stage)
        print(
            "clamped %d dome intensity(ies) to %s, neutralized %d camera "
            "exposure(s)" % (len(i), args.dome_intensity, len(c))
        )
    if not args.no_texture_paths:
        t = normalize_asset_paths(stage)
        print("normalized %d Windows-style asset path(s)" % len(t))
    if not args.no_fileprefix_dedup:
        d = dedup_asset_prefixes(stage)
        print("collapsed %d doubled fileprefix texture path(s)" % len(d))

    out_layer.Save()
    print("wrote overlay '%s'" % output)


if __name__ == "__main__":
    main()

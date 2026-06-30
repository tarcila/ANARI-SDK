# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Overview

HdAnari is an experimental OpenUSD Hydra render delegate that bridges Hydra's scene graph to ANARI rendering. It translates USD/Hydra primitives (meshes, materials, lights, cameras) into ANARI objects for rendering through any ANARI-compliant device.

## Build

From the ANARI-SDK root:

```bash
mkdir build && cd build
cmake -DBUILD_HDANARI=ON \
      -DOPENUSD_ROOT=/path/to/openusd \
      -DOPENUSD_PLUGIN_INSTALL_PREFIX=/path/to/usd/plugin \
      ..
cmake --build . --target hdanari_rd
```

Optional features:
- `-DHDANARI_ENABLE_MDL=ON` — enables NVIDIA MDL material support (requires MDL SDK)
- `-DHDANARI_ENABLE_SDR=ON` — enables SDR/NDR plugin for MDL material discovery

After build, the plugin installs to `${OPENUSD_PLUGIN_INSTALL_PREFIX}/hdanari_rd/`. USD discovers it via `plugInfo.json`.

## Architecture

### Hydra Integration Path

```
USD Application
    ↓ (HdRendererPlugin registry via plugInfo.json)
HdAnariRendererPlugin          → creates HdAnariRenderDelegate
HdAnariRenderDelegate          → creates prims, holds resource registry
HdAnariRenderParam             → shared ANARI device + scene state
HdAnariRenderPass              → manages Frame/Camera/Renderer/World, drives rendering
```

### Prim Hierarchy

- **RPrims** (geometry): `HdAnariMesh`, `HdAnariPoints` — both derive from `HdAnariGeometry`
- **SPrims** (scene state): `HdAnariMaterial`, `HdAnariLight`
- **BPrims** (output buffers): `HdAnariRenderBuffer`
- **Instancer**: `HdAnariInstancer` — flattens nested Hydra instance transforms

### Material System

`HdAnariMaterial` routes to one of four backends depending on the Hydra network:
- `HdAnariMatteMaterial` — simple diffuse (fallback)
- `HdAnariPhysicallyBasedMaterial` — PBR via `UsdPreviewSurface` node translation
- `HdAnariMdlMaterial` — MDL compilation via `HdAnariMdlRegistry` singleton (optional, `HDANARI_ENABLE_MDL`)
- `HdAnariMaterialXMaterial` — MaterialX networks (optional, `HDANARI_ENABLE_MATERIALX`)

`UsdPreviewSurfaceConverter` translates USD's `UsdPreviewSurface` and `UsdUVTexture` nodes. `HdAnariTextureLoader` loads via HIO with color space and filtering metadata.

#### MaterialX backend

`HdAnariMaterialXMaterial` does **no** MaterialX→MDL transcoding itself. It converts the Hydra network to an inline `.mtlx` document via USD's `hdMtlx` (`HdMtlxCreateMtlxDocumentFromHdMaterialNetworkInterface` + `writeToXmlString`) and hands it to a device `materialx` material as `sourceType="documentInline"`; the device transcodes to MDL. Requirements: USD built with `PXR_ENABLE_MATERIALX_SUPPORT`, and an ANARI device advertising the `materialx` subtype (today VisRTX) — otherwise the router falls back to PhysicallyBased/Matte. To make Hydra deliver a MaterialX terminal, `mtlx` is advertised from `GetMaterialRenderContexts()`/`GetShaderSourceTypes()`; device capability is cached as `HdAnariRenderParam::SupportsMaterialX()`.

Wired textures are bound as **host-resolved** ANARI samplers (HIO, like the matte/PBR path), not loaded device-side: after a discovery commit, the backend reads the device's `textureInputs` `STRING_LIST` property (the exact MaterialX paths of bindable `filename` inputs) and binds an `image2D` sampler to each by that path. Baked asset paths are stripped from the document so the device does not attempt (and fail) to resolve them. Materials are prim-owned in v1 (no cross-instance sharing). Fixtures and a render gate live in `test/materialx/` (`verify.py`).

### Geometry & Primvar Binding

`HdAnariGeometry` (base) handles:
- Primvar interpolation (vertex, face-varying, primitive) → ANARI geometry attributes
- Geom subsets with per-subset materials
- Instance transforms from `HdAnariInstancer`

`HdAnariMesh` triangulates faces (including quads/n-gons) via `HdAnariMeshUtil`.

Primvar → ANARI attribute name mapping is driven by token names defined in `anariTokens.h`. Materials declare which primvars they consume; geometry exposes them.

### Scene Versioning & Dirty Tracking

`HdAnariRenderParam` holds an atomic scene version counter. Prims increment it when geometry or lights change. `HdAnariRenderPass` compares versions each frame to decide whether to rebuild the ANARI world. Hydra dirty bits control which prim data is re-synced.

### Optional MDL Components

- `mdl/HdAnariMdlRegistry` — singleton managing MDL SDK (Neuray) lifecycle, module loading, material compilation
- `sdr/HdAnariMdlDiscoveryPlugin` — NDR discovery plugin that reports MDL functions as material nodes
- `sdr/HdAnariMdlParserPlugin` — NDR parser converting MDL introspection data to `SdrShaderNode`

## Key Files

| File | Purpose |
|------|---------|
| `rd/renderDelegate.h/cpp` | Main `HdRenderDelegate`; prim factory, settings tokens |
| `rd/renderPass.h/cpp` | Per-frame ANARI rendering; camera/frame/world management |
| `rd/renderParam.h` | Shared ANARI device; geometry/light registration; scene version |
| `rd/geometry.h/cpp` | Base geometry; primvar binding; instancing |
| `rd/mesh.h/cpp` | Mesh triangulation and attribute upload |
| `rd/material.h/cpp` | Material network routing and texture handling |
| `rd/anariTypes.h` | USD↔ANARI type mappings (GfVec*, GfMatrix* specializations) |
| `rd/anariTokens.h/cpp` | ANARI attribute name tokens |

## Code Conventions

Follows ANARI-SDK style: C++17, Google style via `.clang-format` (80-col, 2-space indent). All Hydra prim classes are prefixed `HdAnari`. ANARI objects are always released via `anari::release()` in destructors.

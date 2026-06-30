# HdAnari MaterialX render fixtures

Scenes and a render gate for the HdAnari MaterialX backend (`HDANARI_ENABLE_MATERIALX`).

| Fixture | Exercises |
|---|---|
| `standard_surface_constant.usda` | MaterialX `standard_surface` with a constant `base_color` → `documentInline` transcode |
| `standard_surface_textured.usda` | `base_color` driven by a wired `ND_image_color3` → host-resolved sampler binding |

Both render **solid green** (`specular` is disabled so the diffuse albedo is unambiguous; a uniform dome light provides illumination). The textured scene references `green.png`, which `verify.py` generates next to the scene at render time (the device cannot resolve the asset path itself — the green texels must come from the HdAnari-loaded sampler).

## Requirements

- HdAnari built and installed with `-DHDANARI_ENABLE_MATERIALX=ON`.
- USD built with `PXR_ENABLE_MATERIALX_SUPPORT` (provides `hdMtlx`).
- An ANARI device that advertises the `materialx` material subtype (today: VisRTX).

## Running the gate

```bash
export PXR_PLUGINPATH_NAME=/path/to/install/lib/usd/plugins   # holds hdanari_rd
export ANARI_LIBRARY=visrtx
export LD_LIBRARY_PATH=/path/to/visrtx/lib:$LD_LIBRARY_PATH    # libanari_library_visrtx.so
python3 verify.py
```

Expected:

```
PASS  standard_surface_constant.usda (constant base_color): center=(0, 255, 0)
PASS  standard_surface_textured.usda (host-resolved wired texture): center=(0, 255, 0)
All MaterialX render gates passed.
```

On a device **without** the `materialx` subtype, the backend falls back to PhysicallyBased/Matte (no crash); the gate's green assertion is specific to a `materialx`-capable device.

## Manual rendering

```bash
usdrecord --renderer Anari --camera /World/Cam standard_surface_constant.usda /tmp/out.png
```

(For the textured scene, generate `green.png` in this directory first, e.g.
`python3 -c "from PIL import Image; Image.new('RGB',(8,8),(0,255,0)).save('green.png')"`.)

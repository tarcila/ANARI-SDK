#!/usr/bin/env python3
# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0
"""HdAnari MaterialX backend render gate.

Renders the bundled MaterialX fixtures through Hydra (usdrecord) on a device
that implements the `materialx` material subtype and asserts the center pixel is
the expected green. There is no CTest harness for HdAnari, so this is a
self-contained, reproducible check.

Prerequisites (set by the caller):
  PXR_PLUGINPATH_NAME  directory holding the installed hdanari_rd plugin
  ANARI_LIBRARY        an ANARI device advertising the `materialx` subtype
                       (today: visrtx)
  LD_LIBRARY_PATH      must contain that device's shared library
  USDRECORD            optional; path to usdrecord (else taken from PATH)

Usage:
  python3 verify.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GREEN_CHANNEL = 1
SCENES = [
    ("standard_surface_constant.usda", "constant base_color"),
    ("standard_surface_textured.usda", "host-resolved wired texture"),
]


def ensure_green_texture(path):
    if os.path.exists(path):
        return
    from PIL import Image  # noqa: deferred so --help works without PIL

    Image.new("RGB", (8, 8), (0, 255, 0)).save(path)


def center_is_green(png):
    from PIL import Image

    im = Image.open(png).convert("RGB")
    w, h = im.size
    r, g, b = im.getpixel((w // 2, h // 2))
    return g > r + 10 and g > b + 10, (r, g, b)


def render(usdrecord, scene, out):
    subprocess.run(
        [usdrecord, "--renderer", "Anari", "--camera", "/World/Cam",
         "--imageWidth", "64", scene, out],
        check=True,
        timeout=300,
    )


def main():
    usdrecord = os.environ.get("USDRECORD") or shutil.which("usdrecord")
    if not usdrecord:
        sys.exit("usdrecord not found; set USDRECORD or add it to PATH")
    if "PXR_PLUGINPATH_NAME" not in os.environ:
        sys.exit("set PXR_PLUGINPATH_NAME to the installed hdanari plugin dir")

    failures = []
    with tempfile.TemporaryDirectory() as work:
        # Render in a temp dir so the fixtures + generated texture stay together
        # and the source tree is untouched.
        for name, _ in SCENES:
            shutil.copy(os.path.join(HERE, name), os.path.join(work, name))
        ensure_green_texture(os.path.join(work, "green.png"))

        for name, desc in SCENES:
            out = os.path.join(work, name + ".png")
            render(usdrecord, os.path.join(work, name), out)
            ok, px = center_is_green(out)
            print(f"{'PASS' if ok else 'FAIL'}  {name} ({desc}): center={px}")
            if not ok:
                failures.append(name)

    if failures:
        sys.exit(f"FAILED: {', '.join(failures)}")
    print("All MaterialX render gates passed.")


if __name__ == "__main__":
    main()

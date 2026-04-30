#!/usr/bin/env python3
"""Regenerate src/practice/practice_owl_tex.c from owl-400.svg.

Run from the repo root whenever owl-400.svg changes:
    python3 tools/gen_owl_logo.py

Then rebuild: make practice -j4
"""
import os
import subprocess
import sys
import tempfile

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("Requires Pillow and numpy: pip install Pillow numpy", file=sys.stderr)
    sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_SVG   = os.path.join(REPO_ROOT, "owl-400.svg")
OUT_C     = os.path.join(REPO_ROOT, "src", "practice", "practice_owl_tex.c")
TEX_W, TEX_H = 32, 32
BLUE_HEX  = "#3399FF"
ALPHA_THRESHOLD = 64  # pixels with alpha <= this → transparent

def main():
    if not os.path.exists(SRC_SVG):
        print(f"Not found: {SRC_SVG}", file=sys.stderr)
        sys.exit(1)

    with open(SRC_SVG) as f:
        svg = f.read()

    # Patch fill to blue
    import re
    blue_svg = re.sub(r'fill="[^"]*"', f'fill="{BLUE_HEX}"', svg)

    tmp_svg = tempfile.NamedTemporaryFile(suffix=".svg", delete=False, mode="w")
    tmp_svg.write(blue_svg)
    tmp_svg.close()

    tmp_png = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
    tmp_png.close()

    try:
        result = subprocess.run(
            ["magick", "-background", "none", "-size", f"{TEX_W}x{TEX_H}",
             tmp_svg.name, "-resize", f"{TEX_W}x{TEX_H}", tmp_png.name],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print(f"magick failed: {result.stderr}", file=sys.stderr)
            sys.exit(1)

        img    = Image.open(tmp_png.name).convert("RGBA")
        pixels = np.array(img)
    finally:
        os.unlink(tmp_svg.name)
        os.unlink(tmp_png.name)

    words = []
    for row in range(TEX_H):
        for col in range(TEX_W):
            r = int(pixels[row, col, 0])
            g = int(pixels[row, col, 1])
            b = int(pixels[row, col, 2])
            a = int(pixels[row, col, 3])
            alpha1 = 1 if a > ALPHA_THRESHOLD else 0
            r5 = (r >> 3) & 0x1F
            g5 = (g >> 3) & 0x1F
            b5 = (b >> 3) & 0x1F
            words.append((r5 << 11) | (g5 << 6) | (b5 << 1) | alpha1)

    lines = [
        '#include "practice.h"',
        "",
        "#ifdef PRACTICE_ROM",
        "",
        f"/* owl-400 icon, {TEX_W}x{TEX_H} RGBA16 5551 blue. Re-generate: python3 tools/gen_owl_logo.py */",
        f"static u16 sPracticeOwlTex[{TEX_W} * {TEX_H}] = {{",
    ]
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        lines.append("    " + ", ".join(f"0x{w:04X}" for w in chunk) + ",")
    lines += [
        "};",
        "",
        "void Practice_Owl_Draw(f32 x, f32 y) {",
        "    RCP_SetupDL_76();",
        "    gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);",
        "    Lib_TextureRect_RGBA16(&gMasterDisp, sPracticeOwlTex, 32, 32, x, y, 1.0f, 1.0f);",
        "}",
        "",
        "#endif",
        "",
    ]

    with open(OUT_C, "w") as f:
        f.write("\n".join(lines))

    opaque = sum(1 for w in words if w & 1)
    print(f"Written {OUT_C}")
    print(f"  {TEX_W}x{TEX_H} RGBA16, {len(words)*2} bytes, {opaque}/{len(words)} opaque pixels")

if __name__ == "__main__":
    main()

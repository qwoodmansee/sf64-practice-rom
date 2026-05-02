#!/usr/bin/env python3
"""Regenerate src/practice/practice_logo_tex.c from HIT64.png.

Run from the repo root whenever HIT64.png changes:
    python3 tools/gen_hit64_logo.py

Then rebuild: make practice -j4
"""
import os
import sys

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("Requires Pillow and numpy: pip install Pillow numpy", file=sys.stderr)
    sys.exit(1)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_PNG   = os.path.join(REPO_ROOT, "HIT64.png")
OUT_C     = os.path.join(REPO_ROOT, "src", "practice", "practice_logo_tex.c")
TEX_W, TEX_H = 64, 32

def main():
    if not os.path.exists(SRC_PNG):
        print(f"Not found: {SRC_PNG}", file=sys.stderr)
        sys.exit(1)

    img = Image.open(SRC_PNG).convert("RGBA")
    arr = np.array(img)

    # Auto-crop to non-black content
    mask = (arr[:,:,0] > 30) | (arr[:,:,1] > 30) | (arr[:,:,2] > 30)
    row_has = np.any(mask, axis=1)
    col_has = np.any(mask, axis=0)
    if not row_has.any() or not col_has.any():
        print(f"Refusing to crop {SRC_PNG}: image has no non-black pixels "
              f"above the threshold. Update HIT64.png with real logo content.",
              file=sys.stderr)
        sys.exit(1)
    rmin = int(np.where(row_has)[0][0])
    rmax = int(np.where(row_has)[0][-1])
    cmin = int(np.where(col_has)[0][0])
    cmax = int(np.where(col_has)[0][-1])
    margin = 15
    rmin = max(0, rmin - margin)
    rmax = min(arr.shape[0] - 1, rmax + margin)
    cmin = max(0, cmin - margin)
    cmax = min(arr.shape[1] - 1, cmax + margin)

    cropped = img.crop((cmin, rmin, cmax + 1, rmax + 1))
    scaled  = cropped.resize((TEX_W, TEX_H), Image.Resampling.LANCZOS)
    pixels  = np.array(scaled)

    words = []
    for row in range(TEX_H):
        for col in range(TEX_W):
            # Cast to Python int — numpy uint8 overflows on << 11
            r = int(pixels[row, col, 0])
            g = int(pixels[row, col, 1])
            b = int(pixels[row, col, 2])
            alpha1 = 0 if (r < 40 and g < 40 and b < 40) else 1
            r5 = (r >> 3) & 0x1F
            g5 = (g >> 3) & 0x1F
            b5 = (b >> 3) & 0x1F
            words.append((r5 << 11) | (g5 << 6) | (b5 << 1) | alpha1)

    lines = [
        '#include "practice.h"',
        "",
        "#ifdef PRACTICE_ROM",
        "",
        f"/* HIT64 logo, {TEX_W}x{TEX_H} RGBA16 5551. Re-generate: python3 tools/gen_hit64_logo.py */",
        f"static u16 sPracticeLogoTex[{TEX_W} * {TEX_H}] = {{",
    ]
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        lines.append("    " + ", ".join(f"0x{w:04X}" for w in chunk) + ",")
    lines += [
        "};",
        "",
        "void Practice_Logo_Draw(f32 x, f32 y) {",
        "    RCP_SetupDL_76();",
        "    gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 255, 255);",
        "    Lib_TextureRect_RGBA16(&gMasterDisp, sPracticeLogoTex, 64, 32, x, y, 1.0f, 1.0f);",
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

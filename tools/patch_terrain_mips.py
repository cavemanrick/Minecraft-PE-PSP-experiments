#!/usr/bin/env python3
"""
Rewrite selected cells in the terrain mipmaps from terrain.png.

Only the cells listed in CELLS are touched; every other cell in the mip
files is left byte-identical. That matters because the mips are NOT a plain
downscale of terrain.png -- 83 of 256 cells in level2 and 140 of 256 in
level3 deliberately differ (hand tuning, and in places genuinely older art).
Regenerating the whole file would silently throw all of that away.

Downscaling is NEAREST, never Lanczos/bilinear. The PSP renderer uses a hard
alpha cutoff (sceGuAlphaFunc GU_GREATER, 0, 0xff), so any filter that invents
intermediate alpha turns into opaque black blotches on hardware. NEAREST also
happens to preserve coverage well on the sparse plant sprites here (the fern
goes 31.6% opaque at 16px -> 35.9% at 8px -> 31.2% at 4px), whereas an
alpha-coverage "max" downscale would fatten it to a 69% blob at 4px.

Usage (from the repo root):
    python3 tools/patch_terrain_mips.py
"""

import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required:  pip install pillow")

BASE = "data/images/terrain.png"
MIPS = ["data/images/terrainMipMapLevel2.png",
        "data/images/terrainMipMapLevel3.png"]

# (col, row, label) in the 16x16 cell grid.
CELLS = [
    # Cocoa growth stages. Non-contiguous because terrain.png has no free
    # cells left: (11,10) is cocoa's original placeholder cell, (13,10) and
    # (13,13) were the only unreferenced cells in the atlas.
    (11, 10, "cocoa age 0"),
    (13, 10, "cocoa age 1"),
    (13, 13, "cocoa age 2"),
    # Ferns. The mips held pre-tinted green art while terrain.png holds a
    # GREYSCALE mask that gets tinted 0xFF339933 at runtime, so the stale
    # mip cells were both wrong art and double-tinted at distance.
    (4, 11, "fern lower"),
    (5, 11, "fern upper"),
]


def main():
    if not os.path.exists(BASE):
        sys.exit("run this from the repo root (%s not found)" % BASE)

    base = Image.open(BASE).convert("RGBA")
    if base.size != (256, 256):
        sys.exit("expected a 256x256 terrain.png, got %dx%d" % base.size)
    bcell = base.size[0] // 16

    for path in MIPS:
        if not os.path.exists(path):
            print("skip (missing): %s" % path)
            continue

        mip = Image.open(path).convert("RGBA")
        cell = mip.size[0] // 16
        if cell < 1:
            sys.exit("%s is too small to hold a 16x16 cell grid" % path)

        for (c, r, label) in CELLS:
            src = base.crop((c * bcell, r * bcell,
                             (c + 1) * bcell, (r + 1) * bcell))
            mip.paste(src.resize((cell, cell), Image.NEAREST),
                      (c * cell, r * cell))

        # Guard the invariant that makes this script necessary: every pixel
        # written must be fully transparent or fully opaque, nothing between.
        px = mip.load()
        bad = 0
        for (c, r, _) in CELLS:
            for y in range(r * cell, (r + 1) * cell):
                for x in range(c * cell, (c + 1) * cell):
                    if px[x, y][3] not in (0, 255):
                        bad += 1
        if bad:
            sys.exit("ABORT: %d non-binary alpha pixels produced in %s"
                     % (bad, path))

        mip.save(path)
        print("patched %s  (%dpx cells, %d cells rewritten)"
              % (path, cell, len(CELLS)))


if __name__ == "__main__":
    main()

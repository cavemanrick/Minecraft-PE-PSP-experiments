#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"

// Huge mushrooms: tree-like structures built from BLOCK_HUGE_MUSHROOM_STEM
// (the stalk) and BLOCK_HUGE_MUSHROOM_CAP (everything else). Two distinct
// shapes:
//   - Brown: single stalk topped by a solid, flat 7x7 cap plate with the
//     four corners clipped off. The stalk stops flush below the cap and
//     never pokes up through it -- from above, only the cap is visible.
//   - Red: single stalk with a hollow box of walls standing off from the
//     stalk (a 5x5 footprint, not touching the 1-wide stalk, so the walls
//     genuinely surround it with a gap), topped by a slightly rounded
//     roof. The walls only extend partway down the stem -- roughly the
//     upper half -- so a clear stretch of bare stem is always visible
//     below the box.
//
// IMPORTANT -- blocks now have real texture wiring (tile.cpp) confirmed
// against the actual atlas: red spotted cap at (13,7), brown pored cap
// at (14,7), stem at (13,8) (best candidate found, worth an in-game
// visual check since it wasn't as unambiguous a match as the two cap
// textures). Solidity/opacity/sound all fall through to sane defaults
// for an unlisted plain-cube block id (see rawSolidPhys/rawCube/
// rawOpaque in tile.cpp) except sound, which is explicitly SOUND_WOOD.
//
// Data convention: BLOCK_HUGE_MUSHROOM_CAP uses HUGE_MUSHROOM_RED_BIT
// (chunk.h) to pick red vs. brown skin texture -- both variants below set
// it accordingly. No per-face cap/pore split is modeled yet (vanilla
// varies texture by block position within the cap via its own metadata
// table); every cap block currently shows the same skin texture on all
// faces. BLOCK_HUGE_MUSHROOM_STEM has no data convention yet -- always 0.

static bool hugeMushroomSpaceClear(World* w, int x, int y, int z, int totalHeight, int capRadius) {
    if (y < 1 || y + totalHeight + 1 > WORLD_H) return false;
    for (int yy = y; yy <= y + totalHeight; yy++) {
        // Stem-only levels just need the 1-wide column; cap/wall levels
        // need the full footprint clear.
        int r = (yy >= y + totalHeight - 3) ? capRadius : 0;
        for (int xx = x - r; xx <= x + r; xx++)
        for (int zz = z - r; zz <= z + r; zz++)
            if (!isTreeClear(worldBlock(w, xx, yy, zz))) return false;
    }
    unsigned char below = worldBlock(w, x, y - 1, z);
    // Huge mushrooms generate on the same ground types regular mushrooms
    // and trees do here; dark forest / mushroom-island-style biomes are
    // expected to route through this same check rather than needing a
    // separate ground-type allowance.
    if (below != BLOCK_GRASS && below != BLOCK_DIRT) return false;
    return true;
}

void mushroomHugeBrown(World* w, Random& random, int x, int y, int z) {
    int stemH = 5 + random.nextInt(4); // 5-8, matches vanilla's normal (non-rare-tall) range
    int R = 3; // 7x7 footprint

    if (!hugeMushroomSpaceClear(w, x, y, z, stemH, R)) return;

    for (int yy = 0; yy < stemH; yy++) {
        setBlock(w, x, y + yy, z, BLOCK_HUGE_MUSHROOM_STEM, 0);
    }

    // Cap: solid 7x7 plate, corners clipped, sitting directly on top of
    // the stem with no gap or hole -- the stem is fully hidden from above.
    // Brown variant: HUGE_MUSHROOM_RED_BIT left unset.
    int capY = y + stemH;
    for (int xx = -R; xx <= R; xx++) {
        for (int zz = -R; zz <= R; zz++) {
            bool corner = (xx == -R || xx == R) && (zz == -R || zz == R);
            if (corner) continue;
            setBlock(w, x + xx, capY, z + zz, BLOCK_HUGE_MUSHROOM_CAP, 0);
        }
    }
}

void mushroomHugeRed(World* w, Random& random, int x, int y, int z) {
    int stemH = 6 + random.nextInt(4); // 6-9
    int R = 2; // 5x5 footprint

    if (!hugeMushroomSpaceClear(w, x, y, z, stemH, R)) return;

    for (int yy = 0; yy < stemH; yy++) {
        setBlock(w, x, y + yy, z, BLOCK_HUGE_MUSHROOM_STEM, 0);
    }

    int topY = y + stemH;
    unsigned char capData = HUGE_MUSHROOM_RED_BIT;
    // Walls scale with stem height but always leave roughly the lower
    // half of the stem exposed below the box, same proportions as the
    // reference render -- never fully enclose the stem however tall it
    // gets.
    int exposedStem = stemH / 2;
    if (exposedStem < 2) exposedStem = 2;
    int wallSpan = stemH - exposedStem;
    if (wallSpan < 2) wallSpan = 2;
    int wallBottomY = topY - wallSpan;
    if (wallBottomY < y) wallBottomY = y;

    // Four walls: only the outer ring of the 5x5 footprint (hollow
    // interior around the stem, standing off from it with a gap -- not
    // touching the 1-wide stalk at all).
    for (int yy = wallBottomY; yy < topY; yy++) {
        for (int xx = -R; xx <= R; xx++) {
            for (int zz = -R; zz <= R; zz++) {
                bool onRing = (xx == -R || xx == R || zz == -R || zz == R);
                if (!onRing) continue;
                setBlock(w, x + xx, yy, z + zz, BLOCK_HUGE_MUSHROOM_CAP, capData);
            }
        }
    }

    // Roof: full 5x5 plate (corners clipped for a rounder silhouette),
    // then a smaller 3x3 layer above for a slightly rounded peak rather
    // than a flat-topped box.
    for (int xx = -R; xx <= R; xx++) {
        for (int zz = -R; zz <= R; zz++) {
            bool corner = (xx == -R || xx == R) && (zz == -R || zz == R);
            if (corner) continue;
            setBlock(w, x + xx, topY, z + zz, BLOCK_HUGE_MUSHROOM_CAP, capData);
        }
    }
    for (int xx = -1; xx <= 1; xx++) {
        for (int zz = -1; zz <= 1; zz++) {
            setBlock(w, x + xx, topY + 1, z + zz, BLOCK_HUGE_MUSHROOM_CAP, capData);
        }
    }
}

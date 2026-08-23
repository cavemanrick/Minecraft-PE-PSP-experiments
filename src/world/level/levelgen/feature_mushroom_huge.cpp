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

// wideFromY is the FIRST y at which the full capRadius footprint is
// occupied, and topY is the LAST y occupied by anything at all. Both are
// passed in by the caller rather than guessed here, because the two
// variants have genuinely different shapes and the old version guessed
// wrong for one of them: it hardcoded the wide band as the top 4 levels
// (yy >= y + totalHeight - 3) and stopped scanning at y + totalHeight.
//
// For the red variant, wallSpan is derived from stemH and reaches 5, and
// the roof adds a layer at topY + 1 -- so for stemH 7, 8 and 9 the lowest
// wall ring(s) and the whole top roof layer sat outside the scanned
// volume. Since neither builder guards its writes with isSolidGen the way
// the tree generators do, those rings were free to overwrite whatever
// terrain happened to be there.
static bool hugeMushroomSpaceClear(World* w, int x, int y, int z,
                                   int topY, int wideFromY, int capRadius) {
    if (y < 1 || topY + 1 >= WORLD_H) return false;
    for (int yy = y; yy <= topY; yy++) {
        // Stem-only levels just need the 1-wide column; cap/wall levels
        // need the full footprint clear.
        int r = (yy >= wideFromY) ? capRadius : 0;
        for (int xx = x - r; xx <= x + r; xx++)
        for (int zz = z - r; zz <= z + r; zz++)
            if (!isTreeClear(worldBlock(w, xx, yy, zz))) return false;
    }
    unsigned char below = worldBlock(w, x, y - 1, z);
    // Grass and dirt as well as mycelium, deliberately: huge mushrooms
    // grow in dark forests too, and those have ordinary grass ground.
    // Restricting this to mycelium (the way vanilla mushroom fields work)
    // would silently kill dark forest mushrooms.
    if (below != BLOCK_GRASS && below != BLOCK_DIRT && below != BLOCK_MYCELIUM) return false;
    return true;
}

void mushroomHugeBrown(World* w, Random& random, int x, int y, int z) {
    int stemH = 5 + random.nextInt(4); // 5-8, matches vanilla's normal (non-rare-tall) range
    int R = 3; // 7x7 footprint

    // Stem occupies y .. y+stemH-1; the single cap plate sits at y+stemH,
    // and that one level is the only wide one.
    int capY = y + stemH;
    if (!hugeMushroomSpaceClear(w, x, y, z, capY, capY, R)) return;

    for (int yy = 0; yy < stemH; yy++) {
        setBlock(w, x, y + yy, z, BLOCK_HUGE_MUSHROOM_STEM, 0);
    }

    // Cap: solid 7x7 plate, corners clipped, sitting directly on top of
    // the stem with no gap or hole -- the stem is fully hidden from above.
    // Brown variant: HUGE_MUSHROOM_RED_BIT left unset.
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

    int roofY = y + stemH;
    unsigned char capData = HUGE_MUSHROOM_RED_BIT;
    // Walls scale with stem height but always leave roughly the lower
    // half of the stem exposed below the box, same proportions as the
    // reference render -- never fully enclose the stem however tall it
    // gets.
    int exposedStem = stemH / 2;
    if (exposedStem < 2) exposedStem = 2;
    int wallSpan = stemH - exposedStem;
    if (wallSpan < 2) wallSpan = 2;
    int wallBottomY = roofY - wallSpan;
    if (wallBottomY < y) wallBottomY = y;

    // The occupied volume runs from the wall ring's bottom (the first wide
    // level) up to the peak at roofY + 1 -- both derived from the same
    // values the builder below actually uses, so the scanned volume can no
    // longer drift out of step with what gets written.
    if (!hugeMushroomSpaceClear(w, x, y, z, roofY + 1, wallBottomY, R)) return;

    for (int yy = 0; yy < stemH; yy++) {
        setBlock(w, x, y + yy, z, BLOCK_HUGE_MUSHROOM_STEM, 0);
    }

    // Four walls: only the outer ring of the 5x5 footprint (hollow
    // interior around the stem, standing off from it with a gap -- not
    // touching the 1-wide stalk at all).
    for (int yy = wallBottomY; yy < roofY; yy++) {
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
            setBlock(w, x + xx, roofY, z + zz, BLOCK_HUGE_MUSHROOM_CAP, capData);
        }
    }
    for (int xx = -1; xx <= 1; xx++) {
        for (int zz = -1; zz <= 1; zz++) {
            setBlock(w, x + xx, roofY + 1, z + zz, BLOCK_HUGE_MUSHROOM_CAP, capData);
        }
    }
}

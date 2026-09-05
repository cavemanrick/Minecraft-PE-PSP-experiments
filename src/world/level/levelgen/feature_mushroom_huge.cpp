#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"

// Vanilla-style huge mushroom generator.
//
// The old PSP implementation used a box-shaped red mushroom and a single
// flat brown plate.  Vanilla 1.8 uses the same compact generator for both:
// brown mushrooms have a broad parasol cap, while red mushrooms have a
// smaller, deeper dome.  The geometry below follows that layout while
// retaining the PSP's separate cap/stem block IDs and red-skin bit.

static bool hugeMushroomSpaceClear(World* w, int x, int y, int z,
                                   int height, int type) {
    if (y < 1 || y + height + 1 >= WORLD_H) return false;

    // Match the vanilla clearance envelope.  Leaves/cap blocks may be
    // replaced, but solid terrain must not be overwritten.
    for (int yy = y; yy <= y + height + 1; ++yy) {
        int r = (yy <= y + 3) ? 0 : 3;
        if (type == 1 && yy <= y + height)
            r = (yy < y + height - 3) ? 0 : 3;
        if (type == 0 && yy < y + height)
            r = 0;

        for (int xx = x - r; xx <= x + r; ++xx)
        for (int zz = z - r; zz <= z + r; ++zz) {
            if (!isTreeClear(worldBlock(w, xx, yy, zz))) return false;
        }
    }

    unsigned char below = worldBlock(w, x, y - 1, z);
    if (below != BLOCK_GRASS && below != BLOCK_DIRT && below != BLOCK_MYCELIUM)
        return false;
    return true;
}

// Metadata values follow the classic huge-mushroom cap pattern.  The PSP
// renderer currently uses bit 0 only to select red/brown texture, so the
// pattern is stored in the upper bits without changing the existing block
// ID or inventory convention.
//
// Currently WRITE-ONLY: see the longer note on this in features.h next to
// the mushroomHugeBrown/mushroomHugeRed declarations. tile.cpp does not
// yet read these bits for BLOCK_HUGE_MUSHROOM_CAP, so every cap face
// still shows the same red/brown pore texture regardless of shape.
static unsigned char capData(int type, int shape) {
    return (unsigned char)((type ? HUGE_MUSHROOM_RED_BIT : 0) |
                           ((shape & 7) << 2));
}

void mushroomHugeBrown(World* w, Random& random, int x, int y, int z) {
    const int type = 0;
    const int height = random.nextInt(3) + 4; // vanilla: 4..6
    if (!hugeMushroomSpaceClear(w, x, y, z, height, type)) return;

    // Brown mushroom: broad parasol, one cap level, 7x7 with clipped
    // corners. This is the characteristic vanilla brown silhouette.
    const int capY = y + height;
    for (int xx = -3; xx <= 3; ++xx) {
        for (int zz = -3; zz <= 3; ++zz) {
            if ((xx == -3 || xx == 3) && (zz == -3 || zz == 3)) continue;

            int shape = 5;
            if (xx == -3) --shape;
            if (xx ==  3) ++shape;
            if (zz == -3) shape -= 3;
            if (zz ==  3) shape += 3;
            if (shape == 5) shape = 0;

            setBlock(w, x + xx, capY, z + zz,
                     BLOCK_HUGE_MUSHROOM_CAP, capData(type, shape));
        }
    }

    // Stem is placed last, matching vanilla and keeping the stalk visible
    // through the center of the cap.
    for (int yy = 0; yy < height; ++yy)
        setBlock(w, x, y + yy, z, BLOCK_HUGE_MUSHROOM_STEM, 0);
}

void mushroomHugeRed(World* w, Random& random, int x, int y, int z) {
    const int type = 1;
    const int height = random.nextInt(3) + 4; // vanilla: 4..6
    if (!hugeMushroomSpaceClear(w, x, y, z, height, type)) return;

    // Red mushroom: a shallow dome, not a hollow box.  The top is 3x3;
    // the lower three cap levels expand to a 5x5 footprint with the
    // characteristic vanilla edge/corner metadata pattern.
    const int capBottom = y + height - 3;
    for (int yy = capBottom; yy <= y + height; ++yy) {
        int radius = (yy == y + height) ? 1 : 2;

        for (int xx = -radius; xx <= radius; ++xx) {
            for (int zz = -radius; zz <= radius; ++zz) {
                int shape = 5;
                if (xx == -radius) --shape;
                if (xx ==  radius) ++shape;
                if (zz == -radius) shape -= 3;
                if (zz ==  radius) shape += 3;

                // The classic generator leaves the center of the lower
                // cap layers open around the stem and uses special corner
                // pieces for the rounded red cap.
                if (yy < y + height) {
                    if ((xx == -radius || xx == radius) &&
                        (zz == -radius || zz == radius))
                        continue;

                    if (shape == 5) shape = 0;
                }

                setBlock(w, x + xx, yy, z + zz,
                         BLOCK_HUGE_MUSHROOM_CAP, capData(type, shape));
            }
        }
    }

    // Stem is placed last, as in vanilla, so it remains visible through
    // the center of the red cap.
    for (int yy = 0; yy < height; ++yy)
        setBlock(w, x, y + yy, z, BLOCK_HUGE_MUSHROOM_STEM, 0);
}

#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"
#include <stdlib.h>

// PSP-friendly dark oak generator.
//
// The old version built a stack of circular blobs and several exposed branch
// stubs. That produced short, lumpy "umbrella" trees rather than the dense,
// irregular dark-oak crowns seen in vanilla. This version keeps the real 2x2
// trunk footprint, but builds the crown from a few cheap, offset leaf layers.
// The layers overlap heavily, so neighboring trees merge into a continuous
// forest ceiling without requiring more tree attempts.

static int darkOakRadiusAt(int layer, int treeHeight, int maxRadius) {
    (void)treeHeight;
    // Bare trunk until the crown. The extra radius covers the 2x2 footprint.
    if (layer < 3) return 1;
    return maxRadius + 1;
}

static bool darkOakGroundOk(World* w, int x, int y, int z) {
    for (int i = 0; i < 4; i++) {
        unsigned char b = worldBlock(w, x + kTrunk2x2Dx[i], y - 1,
                                     z + kTrunk2x2Dz[i]);
        if (b != BLOCK_GRASS && b != BLOCK_DIRT) return false;
    }
    return true;
}

static void darkOakLeafLayer(World* w, Random& random, int cx, int cy, int cz,
                             int radius) {
    for (int xx = cx - radius; xx <= cx + radius; xx++) {
        for (int zz = cz - radius; zz <= cz + radius; zz++) {
            int ax = abs(xx - cx);
            int az = abs(zz - cz);
            bool corner = (ax == radius && az == radius);
            if (corner && random.nextInt(3) != 0) continue;
            if (ax + az > radius + 1) continue;
            if (!isSolidGen(worldBlock(w, xx, cy, zz)) &&
                worldBlock(w, xx, cy, zz) != BLOCK_LOG) {
                setBlock(w, xx, cy, zz, BLOCK_LEAVES_DARK_OAK, 0);
            }
        }
    }
}

static void darkOakBranch(World* w, Random& random, int x, int y, int z,
                          int dx, int dz) {
    // One short diagonal-ish branch is enough to break up the silhouette.
    // Keep it inside the canopy so it doesn't create the old "arms sticking
    // out of an umbrella" appearance.
    int bx = x + dx;
    int bz = z + dz;
    if (!isSolidGen(worldBlock(w, bx, y, bz)))
        setBlock(w, bx, y, bz, BLOCK_LOG, LOG_OAK);
    if (random.nextInt(2) == 0) {
        bx += dx; bz += dz;
        if (!isSolidGen(worldBlock(w, bx, y + 1, bz)))
            setBlock(w, bx, y + 1, bz, BLOCK_LOG, LOG_OAK);
    }
}

void treeDarkOak(World* w, Random& random, int x, int y, int z) {
    // Dark oak is squat, but its crown is broad. A slightly wider height
    // range than the old 6-9 gives the trunk enough vertical separation from
    // the foliage while keeping the PSP generator inexpensive.
    int treeHeight = 7 + random.nextInt(4); // 7-10
    const int maxRadius = 3;

    if (!treeSpaceClear(w, x, y, z, treeHeight, darkOakRadiusAt,
                        maxRadius)) return;
    if (!darkOakGroundOk(w, x, y, z)) return;

    setBlock(w, x, y - 1, z, BLOCK_DIRT);
    trunk2x2BaseDirt(w, x, y, z);

    for (int hh = 0; hh < treeHeight; hh++)
        trunk2x2PlaceLevel(w, x, y + hh, z, LOG_OAK);

    // A few very short internal branch pieces. These are mostly hidden by
    // leaves; their purpose is to make occasional gaps between crowns look
    // natural, not to create visible branch arms.
    static const int DX[4] = { 0, 0, -1, 1 };
    static const int DZ[4] = { -1, 1, 0, 0 };
    int branches = 1 + random.nextInt(3);
    for (int i = 0; i < branches; i++) {
        int d = random.nextInt(4);
        int col = trunk2x2OutwardColumn(random, DX[d], DZ[d]);
        int by = y + 2 + random.nextInt(treeHeight - 3);
        darkOakBranch(w, random,
                      x + kTrunk2x2Dx[col], by,
                      z + kTrunk2x2Dz[col], DX[d], DZ[d]);
    }

    // Broad, overlapping crown. The offset layers are deliberately uneven:
    // vanilla dark oak foliage reads as several adjoining leaf masses rather
    // than a mathematically round sphere.
    const int top = y + treeHeight - 1;
    darkOakLeafLayer(w, random, x,     top - 2, z,     2);
    darkOakLeafLayer(w, random, x - 1, top - 1, z,     3);
    darkOakLeafLayer(w, random, x + 1, top - 1, z - 1, 3);
    darkOakLeafLayer(w, random, x,     top,     z + 1, 3);
    darkOakLeafLayer(w, random, x,     top + 1, z,     2);

    // A small upper crown keeps the top from looking cut off while avoiding
    // the old five-layer circular "roof".
    if (random.nextInt(3) != 0)
        darkOakLeafLayer(w, random, x, top + 2, z, 1);
}

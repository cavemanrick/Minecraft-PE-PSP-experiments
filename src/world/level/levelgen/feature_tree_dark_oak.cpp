#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"
#include <math.h>
#include <stdlib.h>

// Dark oak trees: short, squat 2x2-trunk trees with gnarled branch stubs
// and a wide, thick, deliberately irregular (roof-like) canopy -- not a
// smooth round bulge. Reuses the shared 2x2 trunk helpers from
// features_common.cpp (same footprint convention mega jungle trees use)
// and plain oak leaves/logs, since dark oak doesn't get its own leaf or
// log *data* type in this codebase (LEAF_TYPE_MASK/LOG_TYPE_MASK are both
// full 2-bit fields with all four slots already claimed by
// oak/spruce/birch/jungle -- see chunk.h). Instead:
//   - Leaves use LEAF_OAK with LEAF_DARK_TINT_BIT set, which tells the
//     renderer (tile.cpp) to use a darker, more muted green tint. Every
//     other leaf behavior (decay, drops, tool interaction) is untouched
//     since it's still genuinely LEAF_OAK underneath.
//   - Logs use plain LOG_OAK. Vanilla dark oak wood is a distinctly
//     darker brown than regular oak, but there's no spare log-type slot
//     to express that either; if a visually distinct dark oak *plank/log*
//     texture is wanted later, LOG_TYPE_MASK will need to widen from 2
//     bits to 3 (touches every existing log-color case in tile.cpp, not
//     just this file) -- out of scope here.
//
// Placement (which biomes call treeDarkOak) is left entirely to whatever
// biome/decoration code ends up calling this -- this file only knows how
// to grow the tree once handed a valid (x,y,z), same as every other
// treeXxx function.

static int darkOakRadiusAt(int layerFromBottom, int treeHeight, int maxRadius) {
    // Trunk is bare up to a couple blocks below the canopy; the canopy
    // band itself needs the full clearance. The 2x2 trunk's extra
    // footprint block is already covered by the +1 padding here.
    int canopyStart = treeHeight - 5;
    if (layerFromBottom < canopyStart) return 1;
    return maxRadius + 1;
}

// Places a lumpy, irregular leaf blob layer at height cy, centered near
// (but not exactly at) the trunk, with a ragged edge -- several of these
// stacked and offset from each other is what gives dark oak its wide,
// roof-like, asymmetric canopy silhouette instead of a clean dome.
static void leafBlob(World* w, Random& random, int cx, int cy, int cz, int radius, float centerBiasX, float centerBiasZ) {
    for (int xx = cx - radius - 1; xx <= cx + radius + 1; xx++) {
        float dx = (xx - cx) - centerBiasX;
        for (int zz = cz - radius - 1; zz <= cz + radius + 1; zz++) {
            float dz = (zz - cz) - centerBiasZ;
            float d = (float)sqrt((double)(dx * dx + dz * dz));
            float edge = radius - d;
            if (edge <= 0.2f) continue;
            if (edge < 1.0f && random.nextInt(2) == 0) continue; // ragged edge
            unsigned char leafData = (unsigned char)(LEAF_OAK | LEAF_DARK_TINT_BIT);
            if (!isSolidGen(worldBlock(w, xx, cy, zz)))
                setBlock(w, xx, cy, zz, BLOCK_LEAVES, leafData);
        }
    }
}

// Short, gnarled branch stub jutting off the trunk, ending in its own
// small leaf clump -- dark oak's branches are irregular and asymmetric
// rather than evenly spaced, matching vanilla's actual look.
static void growDarkOakBranch(World* w, Random& random, int x, int y, int z, int dx, int dz) {
    static const unsigned char logData = LOG_OAK;
    int len = 1 + random.nextInt(3); // 1-3 logs, deliberately short/stubby
    int bx = x, by = y, bz = z;
    for (int i = 0; i < len; i++) {
        bx += dx; bz += dz;
        if (i == len - 1 && random.nextInt(2) == 0) by += 1; // occasional upward kink at the tip
        if (!isSolidGen(worldBlock(w, bx, by, bz)))
            setBlock(w, bx, by, bz, BLOCK_LOG, logData);
    }
    leafBlob(w, random, bx, by + 1, bz, 1 + random.nextInt(2), 0.0f, 0.0f);
}

void treeDarkOak(World* w, Random& random, int x, int y, int z) {
    // Short and squat: real dark oak trees are notably shorter than a
    // regular oak despite the much thicker trunk.
    int treeHeight = 6 + random.nextInt(4); // 6-9 tall

    if (!treeSpaceClear(w, x, y, z, treeHeight, darkOakRadiusAt, 4)) return;

    unsigned char below = worldBlock(w, x, y - 1, z);
    if (below != BLOCK_GRASS && below != BLOCK_DIRT) return;
    setBlock(w, x, y - 1, z, BLOCK_DIRT);
    trunk2x2BaseDirt(w, x, y, z);

    // Trunk: solid 2x2 four-column log trunk, bare all the way up.
    for (int hh = 0; hh < treeHeight; hh++) {
        trunk2x2PlaceLevel(w, x, y + hh, z, LOG_OAK);
    }

    // Branch stubs: 2-4 short, irregular branches poking out partway up
    // the trunk, each with a small leaf clump. Directions aren't required
    // to be distinct -- real dark oaks often have branches bunched on one
    // side, which reads as more natural/gnarled than perfectly even
    // spacing would.
    static const int dxs[4] = {  0,  0, -1,  1 };
    static const int dzs[4] = { -1,  1,  0,  0 };
    int branchCount = 2 + random.nextInt(3);
    for (int b = 0; b < branchCount; b++) {
        int dir = random.nextInt(4);
        int originIdx = trunk2x2OutwardColumn(random, dxs[dir], dzs[dir]);
        int ox = x + kTrunk2x2Dx[originIdx], oz = z + kTrunk2x2Dz[originIdx];
        int branchY = y + 2 + random.nextInt(treeHeight - 3 > 1 ? treeHeight - 3 : 1);
        growDarkOakBranch(w, random, ox, branchY, oz, dxs[dir], dzs[dir]);
    }

    // Canopy: several overlapping, ragged-edged leaf blobs stacked and
    // offset from each other, wide relative to the tree's short height.
    // This is what gives the "thick, irregular, roof-like" silhouette
    // instead of a clean round bulge -- no single smooth radius function
    // could produce that lumpiness, so it's built from deliberately
    // off-center overlapping layers instead.
    int canopyBaseY = y + treeHeight - 2;
    leafBlob(w, random, x, canopyBaseY,     z, 3, 0.0f, 0.0f);
    leafBlob(w, random, x, canopyBaseY + 1, z, 4, -0.6f,  0.4f);
    leafBlob(w, random, x, canopyBaseY + 1, z, 4,  0.5f, -0.5f);
    leafBlob(w, random, x, canopyBaseY + 2, z, 3,  0.3f,  0.2f);
    leafBlob(w, random, x, canopyBaseY + 3, z, 2, -0.2f, -0.3f);
}

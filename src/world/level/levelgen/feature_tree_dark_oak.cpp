#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"
#include <math.h>
#include <stdlib.h>

// Dark oak trees: short, squat 2x2-trunk trees with gnarled branch stubs
// and a wide, thick, deliberately irregular (roof-like) canopy -- not a
// smooth round bulge. Reuses the shared 2x2 trunk helpers from
// features_common.cpp (same footprint convention mega jungle trees use).
//
// Leaves are BLOCK_LEAVES_DARK_OAK, a real block id of its own, NOT a
// BLOCK_LEAVES data variant. The earlier design used LEAF_OAK plus a
// LEAF_DARK_TINT_BIT flag, which could never work: block data is a 4-bit
// nibble (worldDataPut masks with 0x0F) and all four bits of the leaf
// nibble are already used, so a bit-4 flag was discarded on every write
// and the tint never rendered. See BLOCK_LEAVES_DARK_OAK's comment in
// chunk.h. isLeaf() covers the new id, so decay and tree-space clearance
// need no special handling here.
//
// Logs are still plain LOG_OAK. Vanilla dark oak wood is a distinctly
// darker brown, but LOG_TYPE_MASK is a full 2-bit field with all four
// slots claimed by oak/spruce/birch/jungle. A visually distinct dark oak
// log needs the same treatment the leaves just got -- its own block id --
// which is a separate change touching every log case in tile.cpp. Left
// undone and flagged rather than quietly shipped as "close enough".
//
// Placement (which biomes call treeDarkOak) is left entirely to whatever
// biome/decoration code ends up calling this -- this file only knows how
// to grow the tree once handed a valid (x,y,z), same as every other
// treeXxx function.

// The 2x2 trunk occupies (x..x+1, z..z+1), so its visual centre is half a
// block off the (x,z) anchor. Canopy blobs are biased by this so the
// canopy sits centred on the trunk instead of hanging half a block toward
// the northwestern column. Branch-tip blobs do NOT use it -- those centre
// on a 1-wide branch tip, which really is at integer coordinates.
#define TRUNK_2X2_CENTER_BIAS 0.5f

// Lowest trunk layer a branch can start from (see treeDarkOak's branchY).
// The clearance function below has to know this, or branches end up
// growing through space that was only ever validated at trunk width.
#define DARK_OAK_FIRST_BRANCH_LAYER 2

static int darkOakRadiusAt(int layerFromBottom, int treeHeight, int maxRadius) {
    (void)treeHeight;
    // Anything at or above the first branch layer needs the full canopy
    // clearance, not just the canopy band itself.
    //
    // This used to be `canopyStart = treeHeight - 5`, which for a 6- or
    // 7-block tree happened to be <= 2 and so covered the branches by
    // accident, but for an 8-block tree gave canopyStart = 3 and for a
    // 9-block tree gave 4 -- leaving layers 2 (and 2-3) validated at
    // radius 1 only, while a branch starting there can reach 5 blocks out.
    // Branches would then be clipped mid-air against unvalidated terrain
    // by growDarkOakBranch's own isSolidGen guard, producing stubby
    // half-branches ending flush against a hillside.
    if (layerFromBottom < DARK_OAK_FIRST_BRANCH_LAYER) return 1;
    return maxRadius + 1;
}

// Places a lumpy, irregular leaf blob layer at height cy, centered near
// (but not exactly at) the trunk, with a ragged edge -- several of these
// stacked and offset from each other is what gives dark oak its wide,
// roof-like, asymmetric canopy silhouette instead of a clean dome.
static void leafBlob(World* w, Random& random, int cx, int cy, int cz, int radius,
                     float centerBiasX, float centerBiasZ) {
    for (int xx = cx - radius - 1; xx <= cx + radius + 1; xx++) {
        float dx = (xx - cx) - centerBiasX;
        for (int zz = cz - radius - 1; zz <= cz + radius + 1; zz++) {
            float dz = (zz - cz) - centerBiasZ;
            float d = (float)sqrt((double)(dx * dx + dz * dz));
            float edge = radius - d;
            if (edge <= 0.2f) continue;
            if (edge < 1.0f && random.nextInt(2) == 0) continue; // ragged edge
            if (!isSolidGen(worldBlock(w, xx, cy, zz)))
                setBlock(w, xx, cy, zz, BLOCK_LEAVES_DARK_OAK, 0);
        }
    }
}

// Short, gnarled branch KNOB jutting off the trunk, ending in its own
// small leaf clump -- dark oak's branches are irregular and asymmetric
// rather than evenly spaced, matching vanilla's actual look.
static void growDarkOakBranch(World* w, Random& random, int x, int y, int z, int dx, int dz) {
    // 1-2 logs, not 1-3. At three the branch reads as a limb reaching out
    // of the silhouette rather than a knob on the trunk, and its tip blob
    // sits far enough out to break the canopy's flat roofline.
    //
    // DARK_OAK_FIRST_BRANCH_LAYER's clearance comment still refers to a
    // 5-block reach; that is now conservative rather than wrong, so the
    // clearance test simply rejects slightly more than it needs to.
    int len = 1 + random.nextInt(2);
    int bx = x, by = y, bz = z;
    for (int i = 0; i < len; i++) {
        bx += dx; bz += dz;
        if (i == len - 1 && random.nextInt(2) == 0) by += 1; // occasional upward kink at the tip
        if (!isSolidGen(worldBlock(w, bx, by, bz)))
            setBlock(w, bx, by, bz, BLOCK_LOG, LOG_OAK);
    }
    leafBlob(w, random, bx, by + 1, bz, 1 + random.nextInt(2), 0.0f, 0.0f);
}

// Ground under all four trunk columns must be grass or dirt. treeSpaceClear
// only checks the (x,z) anchor column, and trunk2x2BaseDirt only converts
// grass to dirt where grass/dirt already is -- so on a cliff edge or a
// one-block ledge three of the four columns could have nothing beneath
// them and the trunk would grow out of thin air.
static bool darkOakGroundOk(World* w, int x, int y, int z) {
    for (int i = 0; i < 4; i++) {
        unsigned char b = worldBlock(w, x + kTrunk2x2Dx[i], y - 1, z + kTrunk2x2Dz[i]);
        if (b != BLOCK_GRASS && b != BLOCK_DIRT) return false;
    }
    return true;
}

void treeDarkOak(World* w, Random& random, int x, int y, int z) {
    // Short and squat: real dark oak trees are notably shorter than a
    // regular oak despite the much thicker trunk.
    int treeHeight = 6 + random.nextInt(4); // 6-9 tall

    // treeSpaceClear already rejects non-grass/dirt ground under the
    // anchor column, so only the other three need checking here (the
    // duplicate anchor test the old code had is folded into
    // darkOakGroundOk).
    if (!treeSpaceClear(w, x, y, z, treeHeight, darkOakRadiusAt, 4)) return;
    if (!darkOakGroundOk(w, x, y, z)) return;

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
        int branchSpan = treeHeight - 3 > 1 ? treeHeight - 3 : 1;
        int branchY = y + DARK_OAK_FIRST_BRANCH_LAYER + random.nextInt(branchSpan);
        growDarkOakBranch(w, random, ox, branchY, oz, dxs[dir], dzs[dir]);
    }

    // Canopy: several overlapping, ragged-edged leaf blobs stacked and
    // offset from each other, wide relative to the tree's short height.
    // This is what gives the "thick, irregular, roof-like" silhouette
    // instead of a clean round bulge -- no single smooth radius function
    // could produce that lumpiness, so it's built from deliberately
    // off-center overlapping layers instead.
    //
    // The per-blob biases below are the artistic offsets; the added
    // TRUNK_2X2_CENTER_BIAS is the mechanical correction that puts the
    // whole stack over the middle of the 2x2 trunk rather than over its
    // northwestern column.
    const float C = TRUNK_2X2_CENTER_BIAS;
    int canopyBaseY = y + treeHeight - 2;
    leafBlob(w, random, x, canopyBaseY,     z, 3,  0.0f + C,  0.0f + C);
    leafBlob(w, random, x, canopyBaseY + 1, z, 4, -0.6f + C,  0.4f + C);
    leafBlob(w, random, x, canopyBaseY + 1, z, 4,  0.5f + C, -0.5f + C);
    leafBlob(w, random, x, canopyBaseY + 2, z, 3,  0.3f + C,  0.2f + C);
    leafBlob(w, random, x, canopyBaseY + 3, z, 2, -0.2f + C, -0.3f + C);
}

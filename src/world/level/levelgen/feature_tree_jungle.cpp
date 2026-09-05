#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"
#include <math.h>
#include <stdlib.h>

// Jungle trees: tall bare trunk topped with a flat, wide, stacked-plate
// canopy (NOT a smoothly tapered radial bulge -- real jungle tree foliage
// is blocky and flat-topped, like layered platforms). Most trees are the
// small regular variant (8-12 tall, single-log 1x1 trunk); a minority roll
// as mega trees, which use vanilla's real 2x2 trunk footprint (four log
// columns, not one thick column) and scale all the way up to the real
// game's ~30-block cap, growing branch stubs along the trunk (count and
// canopy width both scale with height), each branch capped with its own
// small leaf clump. Cocoa pods sprout on free trunk faces near the base,
// within reach regardless of total tree height. Vines hang thickly off the
// canopy underside, branch undersides, and trunk, hugging whichever solid
// wall face supports them (data 2/3/4/5, same convention as ladders)
// rather than floating as loose disconnected chains.

// Cardinal offsets used for BOTH branch growth directions and vine wall
// probing. Index f means "the neighbour at (VDX[f], VDZ[f])".
static const int VDX[4] = {  0,  0, -1,  1 };
static const int VDZ[4] = { -1,  1,  0,  0 };

// VDATA[f] is the vine data byte to write when the support wall is the
// neighbour at (VDX[f], VDZ[f]).
//
// The engine's ladder/vine data convention -- agreed on by all five places
// that read it (emitLadder in mesh_block.cpp, the isLadder branch in
// tile_shapes.cpp, LadderTile::getAABB in tile.cpp, vineWallSolid in
// tile_reed_cactus.cpp, and the BLOCK_LADDER case in tile_support.cpp) --
// is that data names WHERE THE WALL IS, and the quad is drawn flush
// against that same side:
//     data 2 -> wall at z+1     data 3 -> wall at z-1
//     data 4 -> wall at x+1     data 5 -> wall at x-1
//
// So the value paired with each offset is the INVERSE of the naive
// f -> 2+f mapping, because VDX/VDZ are ordered -Z, +Z, -X, +X:
//     f=0 wall at z-1 -> 3     f=1 wall at z+1 -> 2
//     f=2 wall at x-1 -> 5     f=3 wall at x+1 -> 4
//
// Writing { 2, 3, 4, 5 } here (which is what this table used to say) makes
// every generated vine render and collide on the face OPPOSITE its real
// support -- floating in open air on one side with the trunk/leaf face
// eaten on the other. Verified against all five reader tables; do not
// "simplify" this back to 2,3,4,5.
static const unsigned char VDATA[4] = { 3, 2, 5, 4 };

// For a 2x2 mega trunk, (x,z) is the "northwestern" corner log (matching
// vanilla's own convention for where a 2x2 sapling square is anchored);
// the other three columns are at +1 in x and/or z from there. The offset
// tables and placement helpers themselves live in features_common.cpp
// (kTrunk2x2Dx/Dz, trunk2x2BaseDirt, trunk2x2PlaceLevel,
// trunk2x2OutwardColumn) since dark oak will need the same 2x2 footprint
// once darkwood forests are added -- nothing here is jungle-specific.

static int jungleRadiusAt(int layerFromBottom, int treeHeight, int maxRadius) {
    // Only the canopy band needs clearance wider than the bare trunk;
    // layerFromBottom counts up from the base. The +1 padding here already
    // covers a 2x2 trunk's extra footprint block near the base too.
    int canopyStart = treeHeight - 4;
    if (layerFromBottom < canopyStart) return 1;
    return maxRadius + 1;
}

// Drop a hanging vine chain starting just below (vx,vy,vz), attached to
// whichever cardinal wall face actually has solid support there. Does
// nothing if no face qualifies.
static void dropVineChain(World* w, Random& random, int vx, int vy, int vz, int maxChain) {
    // Try faces in a randomized order so the tree doesn't always favor +Z.
    int order[4] = { 0, 1, 2, 3 };
    for (int i = 3; i > 0; i--) {
        int j = random.nextInt(i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    for (int oi = 0; oi < 4; oi++) {
        int f = order[oi];
        int wx = vx + VDX[f], wz = vz + VDZ[f];
        unsigned char wallBlock = worldBlock(w, wx, vy, wz);
        // Must be the SAME predicate vineWallSolid() uses, or generation and
        // survival disagree and the vine is culled on the first neighbour
        // update. isSolidGen() is a pure id test that counts BLOCK_VINE (and
        // torches, cactus, tallgrass, ...) as solid, so it would happily
        // anchor a vine to another vine's side -- which vineWallSolid then
        // rejects, since vine is non-solid. Roughly one vine per chunk was
        // popping out of existence this way.
        if (!isSolidPhys(wallBlock) && !isLeaf(wallBlock)) continue;
        if (worldBlock(w, vx, vy, vz) != BLOCK_AIR) continue;

        unsigned char data = VDATA[f];
        int chainLen = 2 + random.nextInt(maxChain);
        for (int c = 0; c < chainLen; c++) {
            int cy = vy - c;
            if (worldBlock(w, vx, cy, vz) != BLOCK_AIR) break;
            setBlock(w, vx, cy, vz, BLOCK_VINE, data);
            if (random.nextInt(4) == 0) break; // occasionally end a chain early
        }
        return;
    }
}

// Places a flat square-ish leaf plate centered at (cx,cy,cz) with the given
// half-width, trimming the four corners for a softer, less box-like edge
// (still blocky, matching vanilla foliage, just not perfectly square).
static void leafPlate(World* w, Random& random, int cx, int cy, int cz, int half) {
    for (int xx = cx - half; xx <= cx + half; xx++) {
        int axo = xx - cx;
        for (int zz = cz - half; zz <= cz + half; zz++) {
            int azo = zz - cz;
            if (half >= 2 && abs(axo) == half && abs(azo) == half && random.nextInt(2) == 0)
                continue; // trim outer corners
            if (!isSolidGen(worldBlock(w, xx, cy, zz)))
                setBlock(w, xx, cy, zz, BLOCK_LEAVES, LEAF_JUNGLE);
        }
    }
}

// Grows a short branch stub outward from the trunk at a given height,
// ending in its own small leaf clump -- the distinguishing feature of the
// mega jungle tree that a bare trunk + top canopy alone doesn't capture.
// Sheathe a single log column in vines, from `y` upward for as long as the
// column is still log. Used for every trunk in the jungle -- the mega
// tree's four 2x2 columns, the regular jungle tree's single column, and
// the ordinary oaks mixed into the biome.
//
// This is deliberately deterministic rather than the random scatter
// dropVineChain does: "the trunk should be completely covered" is a
// property of the trunk, so it is generated by walking the trunk, not by
// throwing darts at the bounding box and hoping one lands on a log face.
//
// Data convention, re-derived rather than assumed (this file already had
// its VDATA table inverted once): data names WHERE THE WALL IS, so a vine
// sitting at log+off has its wall at -off.
//   off ( 0,-1) -> wall z+1 -> 2      off ( 0,+1) -> wall z-1 -> 3
//   off (+1, 0) -> wall x-1 -> 5      off (-1, 0) -> wall x+1 -> 4
// Note the last two are NOT 4,5 in offset order; do not "tidy" them.
void vineCoatTrunk(World* w, Random& random, int x, int y, int z) {
    static const int  CVX[4] = { 0,  0,  1, -1 };
    static const int  CVZ[4] = { -1, 1,  0,  0 };
    static const unsigned char CVD[4] = { 2, 3, 5, 4 };

    for (int h = 0; h < WORLD_H; ++h) {
        int ly = y + h;
        if (ly + 1 >= WORLD_H) break;
        if (worldBlock(w, x, ly, z) != BLOCK_LOG) break;
        for (int d = 0; d < 4; ++d) {
            // A few bare patches stop the trunk looking shrink-wrapped;
            // 1-in-9 reads as near-total coverage while still varying.
            if (random.nextInt(9) == 0) continue;
            int vx = x + CVX[d], vz = z + CVZ[d];
            if (worldBlock(w, vx, ly, vz) != BLOCK_AIR) continue;
            setBlock(w, vx, ly, vz, BLOCK_VINE, CVD[d]);
        }
    }
}

// A long curtain of vines dropped off the canopy underside, spanning most
// of the canopy's width and falling far enough to reach near the forest
// floor. This is what reads as "jungle" from a distance -- isolated
// strands from dropVineChain do not.
//
// A sheet is a PLANE, so every vine in it carries the same face data:
// otherwise adjacent strands render on different faces of their blocks and
// the curtain looks shredded rather than flat. Run along X -> the quads
// face +/-Z (data 2/3); run along Z -> they face +/-X (data 4/5).
//
// No wall is needed. vineCanSurvive accepts a vine whose block above is a
// leaf or another vine, so the top link hangs off the canopy and each link
// below hangs off the one above it.
static void hangVineSheet(World* w, Random& random, int cx, int cy, int cz, int radius) {
    bool alongX = random.nextInt(2) == 0;
    unsigned char data = alongX ? (unsigned char)(2 + random.nextInt(2))
                                : (unsigned char)(4 + random.nextInt(2));

    // Offset the curtain off the trunk axis so it hangs in open air rather
    // than immediately colliding with the trunk and terminating.
    int off = 2 + random.nextInt(radius > 2 ? radius - 1 : 1);
    if (random.nextInt(2) == 0) off = -off;

    int width = 3 + random.nextInt(radius * 2 - 1);
    int half = width / 2;
    int baseLen = 10 + random.nextInt(13); // 10-22 deep before obstruction

    for (int i = -half; i <= half; i++) {
        int sx = alongX ? cx + i : cx + off;
        int sz = alongX ? cz + off : cz + i;

        // Only hang where there is actually canopy overhead to hang from;
        // this is what keeps the curtain inside the canopy's footprint.
        if (!isLeaf(worldBlock(w, sx, cy, sz))) continue;

        int len = baseLen - random.nextInt(5); // ragged lower edge
        for (int h = 1; h <= len; h++) {
            int vy = cy - h;
            if (vy <= 1) break;
            if (worldBlock(w, sx, vy, sz) != BLOCK_AIR) break;
            setBlock(w, sx, vy, sz, BLOCK_VINE, data);
        }
    }
}

static void growBranch(World* w, Random& random, int x, int y, int z, int dir) {
    int len = 2 + random.nextInt(2); // 2-3 logs out
    int bx = x, by = y, bz = z;
    for (int i = 0; i < len; i++) {
        bx += VDX[dir]; bz += VDZ[dir];
        if (random.nextInt(3) == 0) by += 1; // occasional upward angle
        if (!isSolidGen(worldBlock(w, bx, by, bz)))
            setBlock(w, bx, by, bz, BLOCK_LOG, LOG_JUNGLE);
    }
    leafPlate(w, random, bx, by + 1, bz, 1 + random.nextInt(2));
    // Vines commonly drip off branch leaf clumps.
    int drips = 2 + random.nextInt(3);
    for (int i = 0; i < drips; i++) {
        int dx2 = bx + random.nextInt(3) - 1;
        int dz2 = bz + random.nextInt(3) - 1;
        dropVineChain(w, random, dx2, by, dz2, 5);
    }
}

void treeJungle(World* w, Random& random, int x, int y, int z) {
    // Most jungle trees are the small single-log variant (matches vanilla's
    // "regular jungle tree" being far more common than the mega variant).
    // A minority roll as mega trees, which can range all the way up to the
    // real game's ~30-block cap. Canopy width and branch count both scale
    // with height so a 30-tall tree reads as a correspondingly larger
    // crown, not just a stretched trunk with the same small top.
    bool isMega = random.nextInt(5) == 0; // ~20% mega, matching regular trees being the common case
    int treeHeight = isMega ? (14 + random.nextInt(17))  // mega: 14-30 tall
                             : (8 + random.nextInt(5));   // regular: 8-12 tall

    // Canopy radius grows with height but is capped -- a wider-than-this
    // crown starts reliably straddling multiple 16-block generation chunks,
    // and cross-chunk decoration writes silently drop if the neighboring
    // chunk hasn't generated yet (same limitation basic/spruce trees already
    // live with, just more likely to bite at extreme canopy sizes).
    int maxCanopyRadius = 3 + treeHeight / 8;   // 3-4 for regular, up to 6 for a 30-tall mega
    if (maxCanopyRadius > 6) maxCanopyRadius = 6;

    if (!treeSpaceClear(w, x, y, z, treeHeight, jungleRadiusAt, maxCanopyRadius)) return;

    // NOTE for future jungle temple work: this is the only self-check a
    // jungle tree performs before writing -- if a temple carve runs first
    // in decoration order and leaves solid structure blocks where a tree
    // would spawn, treeSpaceClear naturally reports "not clear" and this
    // call just no-ops, so no temple-awareness needs adding here as long
    // as the temple feature is placed in an earlier phase/decoration pass
    // than treeJungle (currently phase 2 in mcpegen.cpp).

    unsigned char below = worldBlock(w, x, y - 1, z);
    if (below != BLOCK_GRASS && below != BLOCK_DIRT) return;
    setBlock(w, x, y - 1, z, BLOCK_DIRT);
    if (isMega) trunk2x2BaseDirt(w, x, y, z);

    // Canopy: flat stacked plates, widest at the bottom of the canopy band
    // and narrowing by a block (not a smooth taper) each layer going up,
    // giving the flat-topped, layered-platform look of a real jungle tree
    // instead of a rounded bulge. Anchored on the trunk's northwestern
    // corner for both variants -- the canopy radius already dwarfs the
    // 1-block offset a 2x2 trunk's center would otherwise need, and vanilla
    // mega tree canopies aren't perfectly centered either.
    int topY = y + treeHeight;
    leafPlate(w, random, x, topY,     z, maxCanopyRadius);
    leafPlate(w, random, x, topY + 1, z, maxCanopyRadius - 1);
    leafPlate(w, random, x, topY + 2, z, maxCanopyRadius >= 3 ? maxCanopyRadius - 2 : 1);

    // Trunk: solid bare log column(s) up to the canopy. Regular trees use
    // vanilla's 1x1 trunk; mega trees use the shared 2x2 four-column trunk
    // helper (not a single thick column) matching the actual mega jungle
    // tree. Cocoa pods occasionally sprout on a free outer trunk face --
    // only in reach range near the base, not scattered all the way up a
    // 30-tall mega trunk (matches vanilla, where cocoa stays low, and
    // keeps it farmable without extra gear). Cocoa itself is jungle-only,
    // so that part stays local here rather than in the shared helper.
    static const int dx[4] = {  0,  1, 0, -1 };
    static const int dz[4] = { -1,  0, 1,  0 };
    int cocoaBand = treeHeight - 1;
    if (cocoaBand > 8) cocoaBand = 8;
    for (int hh = 0; hh < treeHeight; hh++) {
        if (isMega) {
            trunk2x2PlaceLevel(w, x, y + hh, z, LOG_JUNGLE);
        } else {
            unsigned char t = worldBlock(w, x, y + hh, z);
            if (!isSolidGen(t)) setBlock(w, x, y + hh, z, BLOCK_LOG, LOG_JUNGLE);
        }

        if (hh < 1 || hh >= cocoaBand) continue;
        int trunkCols = isMega ? 4 : 1;
        for (int ci = 0; ci < trunkCols; ci++) {
            if (random.nextInt(6) != 0) continue;
            int lx = x + (isMega ? kTrunk2x2Dx[ci] : 0);
            int lz = z + (isMega ? kTrunk2x2Dz[ci] : 0);
            int dir = random.nextInt(4);
            int px = lx + dx[dir], pz = lz + dz[dir];
            // Skip placing on a face that actually looks into the trunk's
            // own other columns (only matters for the 2x2 case; for 1x1
            // this is always outward already).
            bool intoOwnTrunk = false;
            if (isMega)
                for (int cj = 0; cj < 4; cj++)
                    if (px == x + kTrunk2x2Dx[cj] && pz == z + kTrunk2x2Dz[cj]) { intoOwnTrunk = true; break; }
            if (!intoOwnTrunk && worldBlock(w, px, y + hh, pz) == BLOCK_AIR) {
                int age = random.nextInt(3);
                setBlock(w, px, y + hh, pz, BLOCK_COCOA, (unsigned char)(dir | (age << COCOA_AGE_SHIFT)));
            }
        }
    }

    // Branches: mega jungle trees carry one or more short branch stubs
    // along the trunk, each capped with its own leaf clump. Branch count
    // scales with height so a 30-tall tree reads as a genuinely bigger,
    // busier tree rather than a stretched trunk with the same couple of
    // branches a 14-tall mega tree gets. Only mega-scale trees get any.
    if (isMega) {
        int branchCount = 1 + treeHeight / 8; // ~2 at 14 tall, up to ~4-5 near 30
        // Spread branches across distinct height bands along the trunk
        // (skipping the bare base and the canopy join) so they don't all
        // cluster in the same few blocks on a tall trunk.
        int bandLo = 3, bandHi = treeHeight - 4;
        int bandSpan = bandHi - bandLo;
        if (bandSpan < 1) bandSpan = 1;
        int usedDirs = 0;
        for (int b = 0; b < branchCount; b++) {
            int dir = random.nextInt(4);
            if (branchCount <= 4 && (usedDirs & (1 << dir)) && random.nextInt(3) != 0) continue;
            usedDirs |= (1 << dir);
            int band = (bandSpan * b) / branchCount;
            int branchY = y + bandLo + band + random.nextInt((bandSpan / branchCount) + 1);

            // Start the branch from whichever pair of the 2x2 trunk's
            // columns actually faces outward in this direction, so
            // branches visibly originate from the trunk's outer edge
            // instead of always the same corner log.
            int originIdx = trunk2x2OutwardColumn(random, VDX[dir], VDZ[dir]);
            int ox = x + kTrunk2x2Dx[originIdx], oz = z + kTrunk2x2Dz[originIdx];
            growBranch(w, random, ox, branchY, oz, dir);
        }
    }

    // Vines: thickly hung off the canopy underside, trunk, and branches.
    // Many more starting points than a sparse decorative scattering, each
    // attached to a real solid wall face so they hug the tree rather than
    // floating as isolated chains. Scales with height so a 30-tall tree's
    // much longer trunk doesn't end up looking sparser than a small one.
    int vineStarts = (28 + random.nextInt(14)) * treeHeight / 10;
    for (int i = 0; i < vineStarts; i++) {
        int vx = x + random.nextInt(maxCanopyRadius * 2 + 3) - (maxCanopyRadius + 1);
        int vz = z + random.nextInt(maxCanopyRadius * 2 + 3) - (maxCanopyRadius + 1);
        int vy = y + random.nextInt(treeHeight + 3);
        dropVineChain(w, random, vx, vy, vz, 7);
    }

    // ...and then sheathe the trunk itself. The scatter above hangs vines
    // off the canopy and branches but only coats the trunk where a dart
    // happens to land beside it, which left mega trunks mostly bare. This
    // walks the actual trunk columns, so coverage is a property of the
    // trunk rather than of the RNG.
    //
    // Runs AFTER the scatter on purpose: dropVineChain's own wall test
    // rejects a start position that is already occupied, so coating first
    // would block the hanging chains from anchoring beside the trunk.
    if (isMega) {
        for (int i = 0; i < 4; i++)
            vineCoatTrunk(w, random, x + kTrunk2x2Dx[i], y, z + kTrunk2x2Dz[i]);

        // Long curtains off the canopy underside. Mega trees only: a
        // regular jungle tree's canopy is not wide enough for a sheet to
        // read as anything but a thicker-than-usual strand, and its lower
        // canopy would put the curtain's top at head height.
        //
        // Hung from topY, the widest canopy plate, so the curtain spans
        // the crown rather than a narrow slice of it.
        int sheets = 2 + random.nextInt(2);
        for (int s = 0; s < sheets; s++)
            hangVineSheet(w, random, x, topY, z, maxCanopyRadius);
    } else {
        vineCoatTrunk(w, random, x, y, z);
    }
}

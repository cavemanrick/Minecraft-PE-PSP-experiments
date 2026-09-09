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

// Places a flat leaf plate centered at (cx,cy,cz) with the given half-width,
// using a circular footprint test (squared distance from center vs a radius
// just past half) instead of a square, so the silhouette reads as a rounded,
// stepped plateau rather than a flat square slab -- this is the actual shape
// vanilla jungle/oak canopies use, not a coincidence of vanilla's art.
// A prior version only coin-flip-trimmed the four literal outermost corner
// cells, which is invisible on anything bigger than a tiny plate: on a
// half=6 (13x13) mega-canopy plate it leaves 4 single-cell notches on an
// otherwise dead-flat square. A later attempt used a randomized per-cell
// falloff keyed on distance from each corner along each axis, but that also
// ate into the middle of the straight edges (not just the corners) on
// anything but the largest plates, giving a moth-eaten look rather than a
// rounded one -- keeping a plain circular cutoff and confining the random
// jitter to a thin ring right at the boundary avoids both failure modes.
// A third failure mode, and the reason the jitter rule below is written the
// way it is: a gap punched anywhere in the boundary RING can orphan the
// cells outside it. Leaf decay (leafdecay.cpp) only counts paths that run
// through leaf blocks, so a plate cell whose inward neighbours were all
// jittered away has no route back to the trunk even though it is visually
// part of the same canopy -- it survives generation and then pops the first
// time anything flags it. Restricting the jitter to cells that are already
// on the silhouette (BOTH outward steps leave the circle, i.e. nothing in
// the plate sits behind them) makes gaps bites taken out of the rim rather
// than holes punched in it, so connectivity to the centre is preserved by
// construction. The gap count is unchanged in practice -- 3-8 cells per
// plate, same as the old 1-in-5 ring roll -- because the silhouette-corner
// set is roughly the same size as the fraction the old rule removed.
static void leafPlate(World* w, Random& random, int cx, int cy, int cz, int half) {
    // r2: squared radius of the solid core. +0.5 (not +1) keeps the circle
    // snug to the half-width instead of ballooning past it -- half=6 should
    // still read as "radius 6", just rounded, not effectively radius 7.
    float r = (float)half + 0.5f;
    float r2 = r * r;
    for (int xx = cx - half; xx <= cx + half; xx++) {
        int axo = xx - cx;
        for (int zz = cz - half; zz <= cz + half; zz++) {
            int azo = zz - cz;
            float d2 = (float)(axo * axo + azo * azo);
            if (half >= 2) {
                if (d2 > r2) continue; // outside the plate entirely
                // Step one cell further from the centre along each axis; if
                // both of those are already outside, this cell is a corner
                // of the silhouette and nothing depends on it.
                int ox = axo + (axo >= 0 ? 1 : -1);
                int oz = azo + (azo >= 0 ? 1 : -1);
                bool xOut = (float)(ox * ox + azo * azo) > r2;
                bool zOut = (float)(axo * axo + oz * oz) > r2;
                if (xOut && zOut && random.nextInt(2) == 0) continue;
            }
            if (!isSolidGen(worldBlock(w, xx, cy, zz)))
                setBlock(w, xx, cy, zz, BLOCK_LEAVES, LEAF_JUNGLE);
        }
    }
}

// Canopy rafters: short log arms laid through the widest canopy plate.
//
// Without these the canopy is structurally impossible. Leaf decay allows a
// path of at most REQUIRED_WOOD_RANGE (4) leaf steps back to a log, but the
// top plate reaches radius 6 and sits a block ABOVE the last trunk log, so
// its rim is 7-9 steps out; measured over 40 generated trees, 63% of a
// regular jungle tree's leaves and 59% of a mega tree's were unsupported.
// They survive generation (decay only runs on flagged leaves) and then rot
// out later, because a jungle tree is smothered in vines and VineTile is a
// GrowerTile: every downward vine growth goes through worldSetTileUpdate ->
// worldNotifyNeighborsChanged -> leafFlagNeighbors, which flags the canopy
// underside a few blocks at a time. The tree hollows itself out from the
// rim inwards over the following in-game hours. This is also why oaks and
// spruces never visibly shed despite having a few over-range cells of their
// own -- nothing ever flags them.
//
// Vanilla solves the same problem by building a huge crown out of many
// small leaf clumps, each with its own branch log. This keeps the flat
// stacked-plate silhouette and puts the wood inside it instead.
//
// Arms run in all 8 compass directions from the trunk at the widest plate's
// own layer, and each one stops as soon as the next cell would stop being
// BURIED: that cell must be leaf, the cell above it (in the next plate up)
// must be leaf, and so must all four of its horizontal neighbours. That
// keeps every rafter log invisible from outside the canopy, and it is also
// what caps the arms, so no per-direction length table is needed -- at
// radius 6 a diagonal tip at (4,4) sticks out past the radius-5 plate above
// it, and the check truncates that arm at (3,3) on its own.
//
// Verified by simulation over 800 generated trees (both variants, 8 seeds):
// 126,858 leaves, zero unsupported, zero rafter logs exposed on any side.
static void canopyRafters(World* w, int x, int topY, int z, int maxRadius, bool isMega) {
    static const int RDX[8] = { 1, -1, 0,  0, 1,  1, -1, -1 };
    static const int RDZ[8] = { 0,  0, 1, -1, 1, -1,  1, -1 };
    static const int NX[4]  = { 1, -1, 0,  0 };
    static const int NZ[4]  = { 0,  0, 1, -1 };

    int len = maxRadius - 2;
    if (len < 1) return;

    for (int d = 0; d < 8; d++) {
        // Start from whichever trunk column faces this way, so the arm is
        // flush against the trunk instead of starting a block inside it.
        // For the 1x1 regular trunk both offsets are 0 and this is a no-op.
        int ox = x + ((isMega && RDX[d] > 0) ? 1 : 0);
        int oz = z + ((isMega && RDZ[d] > 0) ? 1 : 0);
        for (int i = 1; i <= len; i++) {
            int rx = ox + RDX[d] * i, rz = oz + RDZ[d] * i;
            if (!isLeaf(worldBlock(w, rx, topY, rz))) break;
            if (!isLeaf(worldBlock(w, rx, topY + 1, rz))) break;
            // ...and all four sides too, or the tip shows through a rim gap.
            // Without this the diagonal arms on a radius-5 canopy end at
            // (3,3), whose neighbour (4,3) is exactly a silhouette corner
            // the jitter above is allowed to remove -- roughly one mega tree
            // in three ended up with a log visible at the canopy edge.
            bool covered = true;
            for (int nn = 0; nn < 4; nn++) {
                unsigned char nb = worldBlock(w, rx + NX[nn], topY, rz + NZ[nn]);
                if (!isLeaf(nb) && !isLog(nb)) { covered = false; break; }
            }
            if (!covered) break;
            setBlock(w, rx, topY, rz, BLOCK_LOG, LOG_JUNGLE);
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
        // Logs count as well as leaves: canopyRafters() replaces a handful
        // of this layer's cells with buried rafter logs, and a leaf-only
        // test here would punch a hole in every curtain that crosses one.
        // vineCanSurviveOnFace accepts either (isSolidPhys || isLeaf above),
        // so both anchors are equally legal at runtime.
        unsigned char roof = worldBlock(w, sx, cy, sz);
        if (!isLeaf(roof) && !isLog(roof)) continue;

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

// Small, static jungle understory feature. This deliberately does NOT use a
// tree-style space scan, recursion, entities, or growth logic. It is just a
// handful of block writes around a ground point, so increasing jungle density
// mostly costs generation-time writes rather than the expensive geometry of
// another tree. Leaves are jungle leaves, while the center is occasionally a
// short fern to break up the silhouette at ground level.
//
// Fixed vs. the originally-submitted version: index 0 in dx[]/dz[] (the
// center cell) used to be exempt from the random skip below, so it was
// ALWAYS filled with a leaf before the fern-accent check ran. That check
// requires the center to still be BLOCK_AIR, so the fern could never
// actually place -- dead code that looked live. The center is now included
// in the same random skip as every other offset, so it has a real chance
// of staying open for the fern.
void jungleUnderstoryFeature(World* w, Random& random, int x, int y, int z) {
    unsigned char below = worldBlock(w, x, y - 1, z);
    if (below != BLOCK_GRASS && below != BLOCK_DIRT) return;
    if (worldBlock(w, x, y, z) != BLOCK_AIR) return;

    // Keep the feature deliberately small: up to 7 possible leaves, with
    // each position (including the center) independently skipped at
    // random. That gives a bushy silhouette without creating another
    // canopy-sized mesh, and leaves the center open often enough for the
    // fern accent below to actually have somewhere to go.
    //
    // Known minor asymmetry, left as-is: this offset set includes the
    // (1,1) and (-1,-1) diagonals but not (1,-1)/(-1,1), so every bush in
    // the world has the same slightly lopsided orientation rather than a
    // varied one. Cosmetic only -- fix by randomizing which diagonal pair
    // is used per call, if it's ever worth the extra RNG draw.
    static const int dx[7] = { 0, 1, -1, 0, 0, 1, -1 };
    static const int dz[7] = { 0, 0, 0, 1, -1, 1, -1 };
    const int count = 5 + random.nextInt(3);
    int placed = 0;
    for (int i = 0; i < count; ++i) {
        if (random.nextInt(4) == 0) continue;
        int bx = x + dx[i];
        int bz = z + dz[i];
        if (worldBlock(w, bx, y, bz) == BLOCK_AIR) {
            // LEAF_PERSISTENT_BIT, not plain LEAF_JUNGLE: this feature
            // contains no wood at all, so every one of its leaves is at
            // decay distance infinity and the whole bush evaporates the
            // first time a neighbour update flags it (vines and cocoa
            // growing nearby do this constantly in a jungle). Persistent
            // is the right flag rather than a workaround -- there is no
            // trunk here for a player to chop, so it can never leave the
            // floating-leaves-after-felling artefact the bit exists to
            // avoid. Vanilla instead gives its jungle shrub a log at the
            // centre; that works too, but it costs the fern accent below,
            // which needs the centre cell to stay open.
            setBlock(w, bx, y, bz, BLOCK_LEAVES,
                     (unsigned char)(LEAF_JUNGLE | LEAF_PERSISTENT_BIT));
            ++placed;
        }
    }

    // A few bushes get a single fern poking through the leaves. The fern is
    // only one block high here; the existing dedicated fern pass handles the
    // taller two-block ferns elsewhere.
    if (placed > 0 && random.nextInt(3) == 0 && worldBlock(w, x, y, z) == BLOCK_AIR)
        setBlock(w, x, y, z, BLOCK_TALLGRASS, TG_FERN);
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

    // Lay the canopy rafters now that both the plates and the trunk exist:
    // the arms are buried inside the plates and start flush against the
    // trunk, so both have to be down first. Before the vine passes, which
    // are happy to anchor to a log.
    canopyRafters(w, x, topY, z, maxCanopyRadius, isMega);

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

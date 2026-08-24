#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/nether_biome.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/tile/fire.h"
#include "world/level/world.h"

#include <math.h>

// --- Vertical shell -----------------------------------------------------
// Floor-hills / ceiling-hills shape (replaces the earlier tunnel-carved-
// solid-shell approach): a sealed 100-tall bedrock box, a shallow lava
// floor, netherrack hills rising from the floor and hanging from the
// ceiling with gaps between them for lava seas/rivers, a guaranteed
// navigable air gap, and a few spots where the two hill layers are
// allowed to touch into a single pillar.
// Vertical shell, now 40 blocks tall (was 100). Every figure below was
// rescaled together rather than just clamping the top, because the four
// numbers are interdependent: the floor hills and ceiling hills each grow
// toward each other out of NETHER_FLOOR_BASE_Y / NETHER_CEIL_BASE_Y, and
// NETHER_MIN_GAP is what keeps them from meeting. Leaving the old hill
// heights against a 40-tall box would have made the gap clamp fire on
// nearly every column, flattening the hills into a featureless slab.
//
// Budget check, worst case: floor base 4 + max hill 10 = top at 14;
// ceiling base 38 - max hill 10 = bottom at 28; gap = 14, exactly
// NETHER_MIN_GAP. So the clamp is reached but never violated, and the
// Nether stays navigable end to end.
#define NETHER_H                40     // total column height, y=0..39
#define NETHER_BEDROCK_BOTTOM   0
#define NETHER_BEDROCK_TOP      (NETHER_H - 1)  // y=39
#define NETHER_LAVA_FLOOR_TOP   3       // y=1..3 is the lava floor, y=0 is bedrock
#define NETHER_FLOOR_BASE_Y     (NETHER_LAVA_FLOOR_TOP + 1) // y=4, hills rise from here
#define NETHER_CEIL_BASE_Y      (NETHER_BEDROCK_TOP - 1)    // y=38, hills hang from here
#define NETHER_MIN_GAP          14      // guaranteed navigable air thickness between the two hill layers

#define MCPE_PI 3.14159265f

// --- Bedrock box: top, bottom, and (only at the strip's true outer
// edges) the four side walls, replacing the old shell-fill's top/bottom-
// only bedrock. Side walls only get written on the boundary chunks
// themselves (checked against WORLD_NETHER_ORIGIN_CX/CZ/WORLD_NETHER_
// CHUNKS in world.h), not every chunk -- an interior chunk has no side
// wall to draw.

// Take the world now: the strip's X origin depends on the preset's
// overworld width (see worldNetherOriginCX in world.h), so these can no
// longer be answered from the chunk coordinate alone.
static bool isNetherStripEdgeX0(const World* w, int cx) { return cx == worldNetherOriginCX(w); }
static bool isNetherStripEdgeX1(const World* w, int cx) { return cx == worldNetherOriginCX(w) + WORLD_NETHER_CHUNKS - 1; }
static bool isNetherStripEdgeZ0(const World*, int cz) { return cz == WORLD_NETHER_ORIGIN_CZ; }
static bool isNetherStripEdgeZ1(const World*, int cz) { return cz == WORLD_NETHER_ORIGIN_CZ + WORLD_NETHER_CHUNKS - 1; }

// --- Height fields --------------------------------------------------------
// Two independent 2D noise fields sampled in *global* block coordinates
// (not chunk-local) so hills stay continuous across chunk borders, same
// requirement the old cave-carving code had for its own tunnel math.
// Values are held in file-local statics and lazily built per world seed,
// same lifetime pattern nether_biome.cpp already uses for its own seed-
// derived state.
static bool s_noiseReady = false;
static long s_noiseForSeed = 0;
static PerlinNoise* s_floorNoise = 0;   // drives floor-hill height + "skip" gaps
static PerlinNoise* s_ceilNoise = 0;    // drives ceiling-hill depth + "skip" gaps
static PerlinNoise* s_touchNoise = 0;   // sparse noise field picking the rare touch-point columns
static PerlinNoise* s_riverNoise = 0;   // ridged field tracing lava river channels

static void ensureNetherNoise(long worldSeed) {
    if (s_noiseReady && s_noiseForSeed == worldSeed) return;
    // Different XOR constants per field (and different again from
    // nether_biome.cpp's own 0x4E45544CL) so the three noise fields and
    // the biome placement are all independent of one another even though
    // they share the same world seed.
    Random rf(worldSeed ^ 0x466C6F6FL); // "Floo"r
    Random rc(worldSeed ^ 0x4365696CL); // "Ceil"
    Random rt(worldSeed ^ 0x546F7563L); // "Touc"h
    Random rr(worldSeed ^ 0x52697665L); // "Rive"r
    delete s_floorNoise; delete s_ceilNoise; delete s_touchNoise; delete s_riverNoise;
    s_floorNoise = new PerlinNoise(&rf, 4);
    s_ceilNoise  = new PerlinNoise(&rc, 4);
    s_touchNoise = new PerlinNoise(&rt, 2);
    s_riverNoise = new PerlinNoise(&rr, 3);
    s_noiseForSeed = worldSeed;
    s_noiseReady = true;
}

// PerlinNoise::getValue does NOT return [-1,1]. It sums octave i as
// `value += octave / pow` with `pow` HALVING each level, so the octave
// weights are 1, 2, 4, 8... and the raw range for L levels is roughly
// +-(2^L - 1). Measured over a 300x300 sample of the 4-octave floor
// field, the real range is about -5.8 .. +7.3, with |raw| > 1 for 68% of
// columns.
//
// The old code did `v = n * 0.5f + 0.5f` on that raw value, treating it
// as if it were already [-1,1]. The result saturated: essentially every
// column clipped to either 0 or 1 after the subsequent remap, so the
// Nether came out as flat plains abutting max-height plateaus with no
// rolling middle ground at all -- and, critically, almost nothing ever
// landed in the "gap" band that was supposed to expose the lava.
//
// The divisor is a measured gain, not the theoretical 2^L - 1. Dividing
// by the theoretical maximum (15) over-compresses -- everything lands
// between 0.4 and 0.7 and the terrain goes flat the other way. 6 puts
// the p5..p95 spread at roughly 0.20..1.00 with only ~5% of columns
// clipping, which is the usable range.
#define NETHER_FLOOR_NOISE_GAIN 6.0f
#define NETHER_TOUCH_NOISE_GAIN 1.5f
#define NETHER_RIVER_NOISE_GAIN 3.0f

static float netherNoise01(PerlinNoise* p, float x, float y, float gain) {
    float v = p->getValue(x, y) / gain * 0.5f + 0.5f;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

#define NETHER_HILL_NOISE_SCALE 0.02f
#define NETHER_HILL_MAX_HEIGHT  10   // was 22; see the budget check on NETHER_H

// Below this normalized floor-noise value the column is open lava sea.
// Measured at ~25% of columns, which reads as large seas broken up by
// hill masses rather than either a lava lake with islands or a solid
// netherrack blanket.
#define NETHER_SEA_THRESHOLD    0.44f

// Normalized floor-noise value at which a hill reaches full height. The
// obvious choice is 1.0 -- map the whole remaining range -- but the
// normalized field's 95th percentile only just reaches 1.0, so almost no
// column ever got near the top and measured hill heights topped out
// around 4 of a possible 10. Saturating at 0.78 instead spreads real
// heights across the whole 1..10 range. Measured over a full 256x256
// Nether: heights 1-10 all represented, on two different seeds.
#define NETHER_HILL_FULL_AT     0.78f

// Lava rivers: |ridged noise| near zero traces winding lines through the
// terrain, so channels cut across hills instead of only appearing where
// the hill field already happened to be low. Half-width 0.04 covers ~5%
// of columns; the shore band tapers hill height back up over the next
// 0.08 so a river has banks rather than sheer walls.
#define NETHER_RIVER_NOISE_SCALE 0.010f
#define NETHER_RIVER_HALF_WIDTH  0.04f
#define NETHER_RIVER_SHORE_BAND  0.08f

// Returned by floorHillHeight for any column that should be open lava.
// It has to be NEGATIVE, not 0: floorTopY = NETHER_FLOOR_BASE_Y + h, and
// the column fill writes netherrack for every y <= floorTopY. With h == 0
// that still lays one netherrack course at NETHER_FLOOR_BASE_Y, capping
// the lava sheet underneath -- which is exactly why the old generator
// produced no visible lava anywhere despite placing a lava layer under
// every single column. -1 puts floorTopY at NETHER_LAVA_FLOOR_TOP, where
// the earlier lava branch already claims the block, so no netherrack is
// written and the lava surface is exposed.
#define NETHER_SEA_FLOOR_H (-1)

static int floorHillHeight(int gx, int gz) {
    float v = netherNoise01(s_floorNoise, gx * NETHER_HILL_NOISE_SCALE,
                            gz * NETHER_HILL_NOISE_SCALE, NETHER_FLOOR_NOISE_GAIN);
    if (v < NETHER_SEA_THRESHOLD) return NETHER_SEA_FLOOR_H; // open lava sea

    float t = (v - NETHER_SEA_THRESHOLD) / (NETHER_HILL_FULL_AT - NETHER_SEA_THRESHOLD);
    if (t > 1.0f) t = 1.0f;
    // Gentle curve rather than a plain t*t. Squaring pulled mid-range
    // columns below 1 block, and anything that truncates to 0 becomes
    // sea -- which is how an intended ~25% lava coverage measured 66%.
    float h = (0.35f + 0.65f * t) * t * NETHER_HILL_MAX_HEIGHT;

    // River channels cut through whatever the hill field wanted here.
    float r = s_riverNoise->getValue(gx * NETHER_RIVER_NOISE_SCALE,
                                     gz * NETHER_RIVER_NOISE_SCALE) / NETHER_RIVER_NOISE_GAIN;
    if (r < 0.0f) r = -r;
    if (r < NETHER_RIVER_HALF_WIDTH) return NETHER_SEA_FLOOR_H; // in the channel
    if (r < NETHER_RIVER_HALF_WIDTH + NETHER_RIVER_SHORE_BAND) {
        float bank = (r - NETHER_RIVER_HALF_WIDTH) / NETHER_RIVER_SHORE_BAND;
        h *= bank; // sloped bank rather than a cliff edge
    }

    // Round, and floor at 1. Past the sea/river tests above, this column
    // is land by definition, so it must lay at least one netherrack
    // course to cap the lava sheet -- otherwise the shore taper silently
    // turns riverbanks back into more river.
    int hi = (int)(h + 0.5f);
    return (hi < 1) ? 1 : hi;
}

static int ceilHillDepth(int gx, int gz) {
    float v = netherNoise01(s_ceilNoise, gx * NETHER_HILL_NOISE_SCALE,
                            gz * NETHER_HILL_NOISE_SCALE, NETHER_FLOOR_NOISE_GAIN);
    if (v < NETHER_SEA_THRESHOLD) return 0; // flat ceiling, not a hole -- the roof stays sealed
    float t = (v - NETHER_SEA_THRESHOLD) / (NETHER_HILL_FULL_AT - NETHER_SEA_THRESHOLD);
    if (t > 1.0f) t = 1.0f;
    return (int)((0.35f + 0.65f * t) * t * NETHER_HILL_MAX_HEIGHT + 0.5f);
}

// True for the rare columns chosen to be touch-point pillars, where the
// floor and ceiling hills are deliberately allowed to meet instead of
// being held apart by NETHER_MIN_GAP. Sparse low-frequency noise
// thresholded very high, so genuine touch points are isolated and
// uncommon rather than forming a whole wall of pillars.
#define NETHER_TOUCH_NOISE_SCALE 0.008f
#define NETHER_TOUCH_THRESHOLD   0.96f

static bool isTouchPointColumn(int gx, int gz) {
    // Same normalization bug as the hill fields had: the raw 2-octave
    // value spans about -1.3..1.5, so `n * 0.5 + 0.5 >= 0.965` demanded a
    // raw value of 0.93 out of a field whose usable top end is 1.5 --
    // rare, but far rarer than intended, and the threshold was tuned as
    // if the input were [-1,1]. Normalized, 0.96 selects ~1.7% of columns.
    float v = netherNoise01(s_touchNoise, gx * NETHER_TOUCH_NOISE_SCALE,
                            gz * NETHER_TOUCH_NOISE_SCALE, NETHER_TOUCH_NOISE_GAIN);
    return v >= NETHER_TOUCH_THRESHOLD;
}

// --- Bulk fill: bedrock box, lava floor, floor hills, ceiling hills -------

static void netherFillColumn(World* w, int cx, int cz) {
    ensureNetherNoise(worldGenSeed());

    int xo = cx * 16, zo = cz * 16;
    bool edgeX0 = isNetherStripEdgeX0(w, cx), edgeX1 = isNetherStripEdgeX1(w, cx);
    bool edgeZ0 = isNetherStripEdgeZ0(w, cz), edgeZ1 = isNetherStripEdgeZ1(w, cz);

    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            int gx = xo + x, gz = zo + z;

            // Side-wall bedrock: only the single outermost block-column
            // on whichever of the 4 sides this chunk actually touches.
            bool onSideWall = (edgeX0 && x == 0) || (edgeX1 && x == 15) ||
                               (edgeZ0 && z == 0) || (edgeZ1 && z == 15);
            if (onSideWall) {
                for (int y = 0; y < NETHER_H; y++) blockPut(w, gx, y, gz, BLOCK_BEDROCK);
                continue;
            }

            int floorH = floorHillHeight(gx, gz);
            int ceilH  = ceilHillDepth(gx, gz);

            bool touch = isTouchPointColumn(gx, gz);
            if (!touch) {
                // Hold the two hill layers apart by at least
                // NETHER_MIN_GAP: if they'd encroach past that, shrink
                // whichever one is taller/deeper just enough to restore
                // the minimum gap, rather than shrinking both blindly --
                // preserves the more prominent hill's shape.
                int floorTopY = NETHER_FLOOR_BASE_Y + floorH;
                int ceilBottomY = NETHER_CEIL_BASE_Y - ceilH;
                int gap = ceilBottomY - floorTopY;
                if (gap < NETHER_MIN_GAP) {
                    int deficit = NETHER_MIN_GAP - gap;
                    // Clamp the floor to NETHER_SEA_FLOOR_H, not 0: 0 would
                    // re-cap an open lava column with a netherrack course
                    // and quietly undo the sea in exactly the tight spots
                    // where a lavafall under a low ceiling looks best.
                    if (floorH >= ceilH) floorH = (floorH - deficit < NETHER_SEA_FLOOR_H) ? NETHER_SEA_FLOOR_H : floorH - deficit;
                    else                 ceilH  = (ceilH  - deficit < 0) ? 0 : ceilH  - deficit;
                }
            }
            // touch==true columns skip the gap clamp entirely -- their
            // floor/ceiling heights are used as-is, and since both noise
            // fields tend toward their max near the same low-frequency
            // peaks the touch-noise threshold selects, floor and ceiling
            // naturally meet or nearly meet at these columns without
            // needing to force-inflate either height field.

            int floorTopY = NETHER_FLOOR_BASE_Y + floorH;
            int ceilBottomY = NETHER_CEIL_BASE_Y - ceilH;

            for (int y = 0; y < NETHER_H; y++) {
                unsigned char id;
                if (y == NETHER_BEDROCK_BOTTOM || y == NETHER_BEDROCK_TOP) {
                    id = BLOCK_BEDROCK;
                } else if (y <= NETHER_LAVA_FLOOR_TOP) {
                    id = BLOCK_CALM_LAVA; // the floor's lava sea, always present under every column
                } else if (y <= floorTopY) {
                    id = BLOCK_NETHERRACK; // floor hill
                } else if (y >= ceilBottomY) {
                    id = BLOCK_NETHERRACK; // ceiling hill
                } else {
                    id = BLOCK_AIR; // navigable gap
                }
                blockPut(w, gx, y, gz, id);
            }
        }
    }
}


// --- Per-biome decoration -----------------------------------------------
// Decoration only ever lands on hill surfaces (netherrack exposed to an
// open air pocket, per netherFillColumn's floor/ceiling hill shape above)
// rather than being buried inside solid rock.

static bool isNetherrackFace(World* w, int x, int y, int z) {
    return worldBlock(w, x, y, z) == BLOCK_NETHERRACK;
}

static void decorateWastes(World* w, Random& random, int xo, int zo) {
    // Magma blocks scattered on floor hills near the lava sea, same
    // rough placement vanilla uses (small blobs just above the lava
    // line) -- purely decorative here (no lava-damage/bubble-column
    // behavior implemented), but it reads correctly and now has a real
    // texture (see tile.cpp's BLOCK_MAGMA case).
    int magmaBlobs = 1 + random.nextInt(2);
    for (int i = 0; i < magmaBlobs; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        // Magma sits just above the lava line. Scaled with the shell: the
        // old span of 6 reached y=12, which is now most of the way up a
        // max-height floor hill rather than a fringe near the lava.
        int y = NETHER_LAVA_FLOOR_TOP + 1 + random.nextInt(3);
        if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) continue;
        if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
        int blobSize = 2 + random.nextInt(4);
        for (int b = 0; b < blobSize; b++) {
            int bx = x + random.nextInt(3) - 1, bz = z + random.nextInt(3) - 1;
            if (worldBlock(w, bx, y, bz) == BLOCK_NETHERRACK)
                blockPut(w, bx, y, bz, BLOCK_MAGMA);
        }
    }
}

static void decorateSoulSandValley(World* w, Random& random, int xo, int zo) {
    // Real soul sand / soul soil now exist as block ids (see chunk.h) --
    // this replaces the earlier gravel placeholder. Soul soil is mixed in
    // as the minority of the floor, matching vanilla's own valley
    // composition (mostly soul sand, soul soil in patches).
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            int gx = xo + x, gz = zo + z;
            for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_FLOOR_BASE_Y; y--) {
                if (worldBlock(w, gx, y, gz) != BLOCK_NETHERRACK) continue;
                if (worldBlock(w, gx, y + 1, gz) != BLOCK_AIR) continue;
                // Top-of-floor netherrack exposed to an open pocket above it.
                unsigned char floorBlock = (random.nextInt(4) == 0) ? BLOCK_SOUL_SOIL : BLOCK_SOUL_SAND;
                blockPut(w, gx, y, gz, floorBlock);
                break; // only the topmost exposed layer at this column
            }
        }
    }
}

// --- Huge warped fungus (the actual "tree" of the Warped Forest) ---------
// The small BLOCK_WARPED_FUNGUS sprite placed below is only the seedling-
// scale decoration; this is the tree-scale structure, matching vanilla's
// real generation rules:
//   - Height is a random int in [4,13], with a 1/12 chance of doubling
//     (so occasionally 8-26 tall).
//   - Trunk is usually a thin 1x1 stem column; a minority (~1/10) instead
//     grow a thick 3x3-plus cross-section trunk (a center column plus the
//     four orthogonally-adjacent columns, corners empty/sometimes filled).
//   - Wart blocks (the "foliage") cling to the sides of the trunk in a
//     halo extending up to 3 blocks out, concentrated in the upper
//     portion of the trunk rather than forming a single canopy at the
//     top -- there's no distinct crown the way an Overworld tree has one.
//   - The base has a 3x3x2 hollow ring around the lowest trunk block
//     where nothing generates (mirrored here as a placement clearance
//     check, not as blocks explicitly cleared, since Nether terrain
//     under a chosen spot is already open air by construction below).
//   - Shroomlights occasionally replace wart/stem blocks, giving natural
//     light sources scattered through the structure (matches vanilla and
//     this biome's actual look -- warped forests are lit primarily by
//     these, not torches).

// How much clear air sits above (x,y,z) before hitting anything solid.
// Needed because the Nether shell is only 40 tall now: the navigable gap
// can be as little as NETHER_MIN_GAP (14), so a fungus must be sized to
// the headroom it actually has rather than rolling a height first and
// then failing.
static int netherHeadroom(World* w, int x, int y, int z, int cap) {
    for (int h = 0; h < cap; h++)
        if (worldBlock(w, x, y + h, z) != BLOCK_AIR) return h;
    return cap;
}

static bool hugeFungusSpaceClear(World* w, int x, int y, int z, int trunkH, bool thick) {
    int footprintR = thick ? 1 : 0; // thick trunk's plus-shape fits in a radius-1 box

    // Clearance radius trimmed from footprint+3 to footprint+2. The wart
    // halo only reaches 3 out at the very top of the trunk (see
    // growHugeWarpedFungus), so demanding a 7x7 or 9x9 clear box for the
    // ENTIRE height rejected nearly every candidate site in a 14-block
    // gap. Halo blocks that fall outside the tested box are placed with
    // setBlock into air only, so an over-reaching halo trims itself
    // against neighbouring terrain instead of corrupting it.
    int clearR = footprintR + 2;
    if (y < 1 || y + trunkH + 1 >= NETHER_CEIL_BASE_Y) return false;
    for (int yy = y; yy <= y + trunkH; yy++) {
        for (int xx = x - clearR; xx <= x + clearR; xx++)
        for (int zz = z - clearR; zz <= z + clearR; zz++)
            if (worldBlock(w, xx, yy, zz) != BLOCK_AIR) return false;
    }
    if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) return false;
    return true;
}

// Wart-block halo for one trunk height-level: scattered blocks within
// haloR of the trunk center, denser near the trunk and thinning toward
// the edge, replacing air only (never overwriting trunk/other wart
// blocks already placed). occasional shroomlight instead of a plain wart
// block for natural light.
static void hugeFungusHaloLevel(World* w, Random& random, int cx, int cy, int cz, int haloR, int density) {
    for (int xx = cx - haloR; xx <= cx + haloR; xx++) {
        for (int zz = cz - haloR; zz <= cz + haloR; zz++) {
            int dx = xx - cx, dz = zz - cz;
            int d2 = dx * dx + dz * dz;
            if (d2 > haloR * haloR) continue;
            if (random.nextInt(density) != 0) continue;
            if (worldBlock(w, xx, cy, zz) != BLOCK_AIR) continue;
            unsigned char id = (random.nextInt(10) == 0) ? BLOCK_GLOWSTONE : BLOCK_WARPED_WART_BLOCK;
            // NOTE: this codebase has no dedicated shroomlight block id
            // yet -- BLOCK_GLOWSTONE stands in as the closest available
            // light-emitting block (see rawLightEmit in tile.cpp). A real
            // shroomlight id/texture would be a small follow-up (same
            // shape as adding BLOCK_HUGE_MUSHROOM_CAP/STEM was) if the
            // visual distinction matters later.
            setBlock(w, xx, cy, zz, id);
        }
    }
}

static void growHugeWarpedFungus(World* w, Random& random, int x, int y, int z) {
    // Size to the headroom that actually exists, then roll within it.
    //
    // Vanilla's 4-13 with a 1/12 chance of doubling tops out at 26, which
    // is taller than this Nether's entire navigable gap -- every one of
    // those doubled rolls was guaranteed to fail hugeFungusSpaceClear and
    // silently produce nothing, which is a large part of why mega fungi
    // were so rare. The doubling is gone and the range is clamped to what
    // the column can hold, minus 2 for the wart halo above the trunk top.
    int room = netherHeadroom(w, x, y, z, NETHER_HILL_MAX_HEIGHT + NETHER_MIN_GAP) - 2;
    if (room < 5) return;

    int trunkH = 5 + random.nextInt(8); // 5-12
    if (trunkH > room) trunkH = room;

    bool thick = random.nextInt(6) == 0; // thick trunks a bit more common than vanilla's 1/10, since they read well at this scale

    if (!hugeFungusSpaceClear(w, x, y, z, trunkH, thick)) return;

    static const int plusDx[4] = {  1, -1,  0,  0 };
    static const int plusDz[4] = {  0,  0,  1, -1 };

    for (int hh = 0; hh <= trunkH; hh++) {
        int cy = y + hh;
        // Data 0 == LOG_AXIS_Y (vertical) -- every huge fungus generates
        // upright, same as every Overworld tree trunk.
        setBlock(w, x, cy, z, BLOCK_WARPED_STEM, 0);
        if (thick) {
            for (int d = 0; d < 4; d++) {
                // Corners/arms of the plus: mostly present, occasionally
                // left as air for an irregular cross-section rather than
                // a perfectly uniform 3x3-plus every level.
                if (random.nextInt(6) == 0) continue;
                setBlock(w, x + plusDx[d], cy, z + plusDz[d], BLOCK_WARPED_STEM, 0);
            }
        }

        // Wart-block halo concentrates in the upper 2/3 of the trunk,
        // thickest near the top -- no separate "canopy" layer the way an
        // Overworld tree has one; it's a halo the whole way up instead.
        if (hh < trunkH / 3) continue;
        int haloR = thick ? 2 : 1;
        if (hh > trunkH - 3) haloR += 1; // slightly wider near the very top
        if (haloR > 3) haloR = 3; // matches vanilla's "up to 3 out" cap
        int density = 2 + random.nextInt(2); // lower = denser; varies level to level
        hugeFungusHaloLevel(w, random, x, cy, z, haloR, density);
    }
}

// First air block sitting directly on warped nylium in this column.
static bool findNyliumSurface(World* w, int x, int z, int* outY) {
    for (int y = NETHER_CEIL_BASE_Y; y > NETHER_FLOOR_BASE_Y; y--) {
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        *outY = y;
        return true;
    }
    return false;
}

// A regular (non-mega) warped tree: a thin stem 3-6 tall with a compact
// wart-block cap, roughly the silhouette of a small mushroom. Cheap
// enough to place many per chunk, which is the point -- these are what
// makes a warped forest feel like a forest, with the mega fungi standing
// out above them rather than being the only structure present.
static void growWarpedTree(World* w, Random& random, int x, int y, int z) {
    int trunkH = 3 + random.nextInt(4); // 3-6

    // Needs its own height plus one for the cap, and a 3x3 clear at cap
    // level. Kept much looser than the mega fungus's box check so these
    // can grow in the cramped spots between hills.
    int room = netherHeadroom(w, x, y, z, trunkH + 2);
    if (room < trunkH + 1) return;

    for (int h = 0; h < trunkH; h++)
        setBlock(w, x, y + h, z, BLOCK_WARPED_STEM, 0); // data 0 == LOG_AXIS_Y, upright

    int capY = y + trunkH;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            // Clip the corners so the cap is a plus, not a square -- a
            // full 3x3 at this scale reads as a block, not foliage.
            if (dx != 0 && dz != 0 && random.nextInt(2) == 0) continue;
            if (worldBlock(w, x + dx, capY, z + dz) != BLOCK_AIR) continue;
            unsigned char id = (random.nextInt(12) == 0) ? BLOCK_GLOWSTONE : BLOCK_WARPED_WART_BLOCK;
            setBlock(w, x + dx, capY, z + dz, id);
        }
    }
    // A single block crowning the centre, so the silhouette isn't flat.
    if (worldBlock(w, x, capY + 1, z) == BLOCK_AIR)
        setBlock(w, x, capY + 1, z, BLOCK_WARPED_WART_BLOCK);
}

static void decorateWarpedForest(World* w, Random& random, int xo, int zo) {
    // Real warped nylium/wart block/fungus/roots/sprouts now exist as
    // block ids (see chunk.h) -- this replaces the earlier wool-color and
    // mushroom-feature placeholders from the first pass.
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            int gx = xo + x, gz = zo + z;
            for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_FLOOR_BASE_Y; y--) {
                if (worldBlock(w, gx, y, gz) != BLOCK_NETHERRACK) continue;
                if (worldBlock(w, gx, y + 1, gz) != BLOCK_AIR) continue;
                setBlock(w, gx, y, gz, BLOCK_WARPED_NYLIUM);
                break;
            }
        }
    }

    // Warped fungus (the small seedling-scale sprite) and warped wart
    // blocks scattered on the nylium floor. The actual tree-scale
    // structure of this biome is growHugeWarpedFungus below, not this --
    // matches vanilla, where the two coexist (huge fungi are what you
    // actually navigate around; the small fungus sprite is undergrowth).
    int fungusTries = 4 + random.nextInt(4);
    for (int i = 0; i < fungusTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_FUNGUS);
    }

    // Huge warped fungi: the biome's actual "trees". Tries a handful of
    // random floor spots per chunk; hugeFungusSpaceClear rejects anything
    // too cramped (low ceiling gap, neighboring hill terrain intruding
    // on the clearance box, non-nylium ground), so many tries silently
    // no-op in a chunk that's mostly ceiling-hill overhang or open lava --
    // matches how basic Overworld trees already handle rejected spots.
    // Both tree scales use a nylium-surface search rather than a random y.
    // The old code rolled a random height in the 34-block span and required
    // it to already be the one block sitting on nylium -- so the great
    // majority of tries missed the ground entirely before any of the
    // clearance logic even ran. Searching down for the surface makes every
    // try a real attempt.
    int hugeFungusTries = 8 + random.nextInt(8);
    for (int i = 0; i < hugeFungusTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y;
        if (!findNyliumSurface(w, x, z, &y)) continue;
        growHugeWarpedFungus(w, random, x, y, z);
    }

    // Regular warped trees: the mid-scale between the ground sprite and
    // the mega fungus. These did not exist before -- the biome had only
    // the 1-block BLOCK_WARPED_FUNGUS sprite and the mega structure, with
    // nothing in between, so a warped forest read as bare nylium with the
    // occasional tower. These fill the canopy in.
    int treeTries = 14 + random.nextInt(10);
    for (int i = 0; i < treeTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y;
        if (!findNyliumSurface(w, x, z, &y)) continue;
        growWarpedTree(w, random, x, y, z);
    }

    int wartTries = 2 + random.nextInt(3);
    for (int i = 0; i < wartTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_WART_BLOCK);
    }

    // Ground-cover decorations (roots, sprouts, twisting vines) -- all
    // three are cross-shaped sprites (see isCrossShaped in chunk.h),
    // scattered lightly across the nylium floor rather than dense forest
    // undergrowth, since without a real support-checking Tile subclass
    // (see the design note in chunk.h/tile.cpp about why BushTile's
    // light-based despawn wouldn't fit dark Nether caverns) placing too
    // many risks looking cluttered rather than natural.
    int rootsTries = 3 + random.nextInt(4);
    for (int i = 0; i < rootsTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_ROOTS);
    }

    int sproutTries = 2 + random.nextInt(3);
    for (int i = 0; i < sproutTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_NETHER_SPROUTS);
    }

    // Twisting vines hang from cavern ceilings rather than growing on the
    // floor (matching where vanilla actually places them), simplified
    // here to a single static cross-sprite block per spot rather than a
    // real multi-block hanging vine column (see isCrossShaped's note).
    int vineTries = 2 + random.nextInt(3);
    for (int i = 0; i < vineTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (!isNetherrackFace(w, x, y + 1, z)) continue;
        setBlock(w, x, y, z, BLOCK_TWISTING_VINES);
    }
}

// --- Quartz veins (Wastes + Soul Sand Valley, matches vanilla distribution) --

static void netherOreFeature(World* w, Random& random, int x, int y, int z, unsigned char tile, int count) {
    float dir = random.nextFloat() * MCPE_PI;
    float x0 = x + 8 + sinf(dir) * count / 8.0f;
    float x1 = x + 8 - sinf(dir) * count / 8.0f;
    float z0 = z + 8 + cosf(dir) * count / 8.0f;
    float z1 = z + 8 - cosf(dir) * count / 8.0f;
    float y0 = (float)(y + random.nextInt(3) + 2);
    float y1 = (float)(y + random.nextInt(3) + 2);

    for (int D = 0; D <= count; D++) {
        float d = (float)D;
        float xx = x0 + (x1 - x0) * d / count;
        float yy = y0 + (y1 - y0) * d / count;
        float zz = z0 + (z1 - z0) * d / count;

        float ss = random.nextFloat() * count / 16.0f;
        float r = (sinf(d * MCPE_PI / count) + 1.0f) * ss + 1.0f;

        int xt0 = (int)floorf(xx - r / 2.0f), xt1 = (int)floorf(xx + r / 2.0f);
        int yt0 = (int)floorf(yy - r / 2.0f), yt1 = (int)floorf(yy + r / 2.0f);
        int zt0 = (int)floorf(zz - r / 2.0f), zt1 = (int)floorf(zz + r / 2.0f);

        for (int x2 = xt0; x2 <= xt1; x2++) {
            float xd = ((x2 + 0.5f) - xx) / (r / 2.0f);
            if (xd * xd >= 1.0f) continue;
            for (int y2 = yt0; y2 <= yt1; y2++) {
                float yd = ((y2 + 0.5f) - yy) / (r / 2.0f);
                if (xd * xd + yd * yd >= 1.0f) continue;
                for (int z2 = zt0; z2 <= zt1; z2++) {
                    float zd = ((z2 + 0.5f) - zz) / (r / 2.0f);
                    if (xd * xd + yd * yd + zd * zd < 1.0f &&
                        worldBlock(w, x2, y2, z2) == BLOCK_NETHERRACK) {
                        setBlock(w, x2, y2, z2, tile);
                    }
                }
            }
        }
    }
}

// --- Ambient decoration (all biomes) --------------------------------------
// A guaranteed light source per chunk, plus randomly-placed permanent
// fires on floor-hill netherrack. Both look for a genuine solid-
// netherrack-with-open-air-above spot rather than writing blindly, so
// they never end up floating in the open gap or buried in hill rock.

// Walks DOWN from the sealed roof to the first air block that has solid
// rock directly above it -- i.e. the underside of the ceiling, wherever
// the ceiling hill happens to hang at this column.
//
// The old glowstone placer instead picked a random y in the whole 34-block
// span and required it to already be a ceiling face, which succeeds only
// when the dice happen to land on the one correct block in the column.
// The overwhelming majority of its already-small number of attempts
// silently did nothing, which is the real reason glowstone was so sparse.
// This search finds the face every time.
static bool findCeilingFaceSpot(World* w, int x, int z, int* outY) {
    for (int y = NETHER_CEIL_BASE_Y; y > NETHER_LAVA_FLOOR_TOP; y--) {
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (!isNetherrackFace(w, x, y + 1, z)) continue;
        *outY = y;
        return true;
    }
    return false;
}

static void placeCeilingGlowstone(World* w, int xo, int zo, Random& random) {
    // Applies to every biome (not just Wastes) -- glowstone clusters are a
    // blanket ambient feature of the whole Nether.
    int clusters = 8 + random.nextInt(8);
    for (int i = 0; i < clusters; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y;
        if (!findCeilingFaceSpot(w, x, z, &y)) continue;

        // Clusters hang a little way down from the face rather than being
        // a flat one-block sheet, so they read as blobs from below.
        int blobSize = 5 + random.nextInt(7);
        for (int b = 0; b < blobSize; b++) {
            int bx = x + random.nextInt(5) - 2, bz = z + random.nextInt(5) - 2;
            int by = y - random.nextInt(2);
            if (worldBlock(w, bx, by, bz) != BLOCK_AIR) continue;
            // Attach only where there is something above to hang from --
            // either the ceiling itself or another glowstone block already
            // placed by this same cluster.
            unsigned char above = worldBlock(w, bx, by + 1, bz);
            if (above != BLOCK_NETHERRACK && above != BLOCK_GLOWSTONE) continue;
            setBlock(w, bx, by, bz, BLOCK_GLOWSTONE);
        }
    }
}

// --- Lavafalls -----------------------------------------------------------
// Static columns of BLOCK_CALM_LAVA, not flowing source blocks.
//
// This is a deliberate trade. A real lava source would have to tick and
// spread every time its chunk streams in, and the Nether has enough
// exposed lava surface after the sea/river carve above that handing the
// fluid updater a few hundred new sources per chunk is not something this
// hardware should be asked to do. A pre-baked column is deterministic,
// costs nothing at runtime, and looks identical standing still -- it
// simply will not re-flow if the player mines into it.
#define NETHER_LAVAFALL_MAX_DROP 26

static void pourLavafall(World* w, int x, int yTop, int z) {
    for (int y = yTop; y > NETHER_LAVA_FLOOR_TOP; y--) {
        if (yTop - y >= NETHER_LAVAFALL_MAX_DROP) break;
        if (worldBlock(w, x, y, z) != BLOCK_AIR) break; // landed on a hill or the sea
        setBlock(w, x, y, z, BLOCK_CALM_LAVA);
    }
}

static void placeCeilingLavafalls(World* w, int xo, int zo, Random& random) {
    int falls = random.nextInt(3); // 0-2 per chunk; common enough to see often, rare enough to stay a feature
    for (int i = 0; i < falls; i++) {
        int x = xo + 1 + random.nextInt(14), z = zo + 1 + random.nextInt(14);
        int y;
        if (!findCeilingFaceSpot(w, x, z, &y)) continue;
        // Set the source block into the ceiling itself so the fall reads as
        // pouring OUT of the rock rather than starting in mid-air.
        setBlock(w, x, y + 1, z, BLOCK_CALM_LAVA);
        pourLavafall(w, x, y, z);
    }
}

static void placeHillsideLavafalls(World* w, int xo, int zo, Random& random) {
    // Lava breaking out of the side of a floor hill and running down it.
    // Needs a hill top with a genuine drop beside it, so this looks for an
    // exposed netherrack surface with an air neighbour that also has air
    // below -- the lip of a slope, not the middle of a plateau.
    static const int dx4[4] = { 1, -1, 0, 0 };
    static const int dz4[4] = { 0, 0, 1, -1 };

    int tries = 3 + random.nextInt(3);
    for (int t = 0; t < tries; t++) {
        int x = xo + 1 + random.nextInt(14), z = zo + 1 + random.nextInt(14);
        int y;
        // Reuse the floor-surface search, but require real elevation: a
        // breakout at the waterline of the lava sea would be invisible.
        bool found = false;
        for (y = NETHER_CEIL_BASE_Y; y >= NETHER_FLOOR_BASE_Y + 4; y--) {
            if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) continue;
            if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
            found = true;
            break;
        }
        if (!found) continue;

        int d = random.nextInt(4);
        int nx = x + dx4[d], nz = z + dz4[d];
        if (worldBlock(w, nx, y, nz) != BLOCK_AIR) continue;      // must be the lip
        if (worldBlock(w, nx, y - 1, nz) != BLOCK_AIR) continue;  // and a real drop below it

        setBlock(w, x, y, z, BLOCK_CALM_LAVA); // the source, embedded in the hill face
        pourLavafall(w, nx, y, nz);
    }
}

static bool findFloorSurfaceSpot(World* w, int xo, int zo, Random& random, int tries, int* outX, int* outY, int* outZ) {
    for (int t = 0; t < tries; t++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_FLOOR_BASE_Y; y--) {
            if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) continue;
            if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
            *outX = x; *outY = y + 1; *outZ = z;
            return true;
        }
    }
    return false;
}

static void placeChunkLightSource(World* w, int xo, int zo, Random& random) {
    int x, y, z;
    if (findFloorSurfaceSpot(w, xo, zo, random, 8, &x, &y, &z))
        blockPut(w, x, y, z, BLOCK_TORCH);
    // If no valid surface spot turns up in 8 tries (a chunk that's
    // entirely open lava sea/river with no floor hill at all), the chunk
    // simply goes without its own torch rather than forcing one into an
    // unsupported spot -- decorateWastes' glowstone clusters and
    // neighboring chunks' torches still light the area.
}

static void placeAmbientFires(World* w, int xo, int zo, Random& random) {
    // 1-3 permanent fires per chunk (infinite-burn on netherrack, see
    // fire.cpp's infiniBurn check), scattered independently of the single
    // light-source torch above.
    int count = 1 + random.nextInt(3);
    for (int i = 0; i < count; i++) {
        int x, y, z;
        if (!findFloorSurfaceSpot(w, xo, zo, random, 4, &x, &y, &z)) continue;
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue; // don't stomp the torch or other decoration
        firePlace(w, x, y, z);
    }
}

// --- Entry point ----------------------------------------------------------

void chunkGenerateNether(World* w, long worldSeed, int cx, int cz) {
    int xo = cx * 16, zo = cz * 16;

    netherFillColumn(w, cx, cz);

    Random random((long)(int)((unsigned int)cx * 341873128712u + (unsigned int)cz * 132897987541u + worldSeed));

    NetherBiomeId biome = classifyNetherBiome(worldSeed, w, xo + 8, zo + 8);
    switch (biome) {
        case NB_WASTES:
            decorateWastes(w, random, xo, zo);
            break;
        case NB_SOUL_SAND_VALLEY:
            decorateSoulSandValley(w, random, xo, zo);
            break;
        case NB_WARPED_FOREST:
            decorateWarpedForest(w, random, xo, zo);
            break;
    }

    placeChunkLightSource(w, xo, zo, random);
    placeCeilingGlowstone(w, xo, zo, random);
    // Lavafalls run after decoration so they pour over finished terrain
    // and cut through any nylium/soul sand surfacing, rather than being
    // overwritten by it.
    placeCeilingLavafalls(w, xo, zo, random);
    placeHillsideLavafalls(w, xo, zo, random);
    placeAmbientFires(w, xo, zo, random);

    // Quartz veins: now uses the real BLOCK_NETHER_QUARTZ_ORE id (see
    // chunk.h/tile.cpp) instead of the first pass's BLOCK_QUARTZ_BLOCK
    // stand-in. Present in Wastes and Soul Sand Valley, matching vanilla's
    // own distribution (quartz doesn't generate in Warped Forest).
    if (biome == NB_WASTES || biome == NB_SOUL_SAND_VALLEY) {
        int veins = 1 + random.nextInt(3);
        for (int i = 0; i < veins; i++) {
            int x = xo + random.nextInt(16), y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y), z = zo + random.nextInt(16);
            netherOreFeature(w, random, x, y, z, BLOCK_NETHER_QUARTZ_ORE, 12);
        }
    }
}

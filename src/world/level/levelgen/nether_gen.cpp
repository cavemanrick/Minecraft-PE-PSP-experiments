#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/nether_fortress_gen.h"
#include "world/level/levelgen/nether_biome.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/levelgen/mcpegen.h"
#include "world/level/levelgen/gen_features.h"
#include "world/level/tile/fire.h"
#include "world/level/world.h"

#include <math.h>
#include <stdlib.h>  // qsort -- noise-field calibration (see ensureNetherNoise)

// --- Vertical shell -----------------------------------------------------
// SOLID-FIRST model: the Nether begins as a slab of netherrack twenty
// blocks deep, and lava is CARVED OUT of it. Land is the default state;
// seas and rivers are subtractive.
//
// This replaces a floor-hills model that worked the other way round: a
// permanent three-block lava sheet spanning the entire strip, with
// netherrack hills standing on top of it, so a column was land only where
// the hill noise cleared a fixed threshold. That coupled "how much lava
// exists" to the ABSOLUTE value of a Perlin field -- and that field
// carries a large per-seed constant offset (see the calibration note
// further down), so whole worlds came out as unbroken lava with no floor
// at all. Measured over ten seeds, land coverage ran 13%-86%, and on the
// low seeds every local window sampled was 100% sea.
//
// Carving inverts the dependency. An uncarved column is land by
// construction, so the worst a badly-offset noise field can now do is
// give a world slightly more or slightly fewer seas than intended -- it
// can no longer remove the ground.
#define NETHER_H                48    // total column height, y=0..47

// Raised from 40. This is free: block storage is sectioned in 16-block
// slices (SECTION_SY in chunk.h), and y=0..47 occupies sections 0-2 --
// exactly the same three sections y=0..39 already did. The extra eight
// blocks cost no memory and buy the headroom a twenty-deep floor slab
// needs in order to still leave a navigable cavern above it.
#define NETHER_BEDROCK_BOTTOM   0
#define NETHER_BEDROCK_TOP      (NETHER_H - 1)   // y=47

// The lava level: every carved basin floods to exactly this height, the
// way the Overworld has one sea level. "Twenty blocks of netherrack" is
// literally this number -- an uncarved column is solid netherrack from
// y=1 to y=20 before any hill relief is added on top of it.
#define NETHER_LAVA_LEVEL_Y     20

// Deepest a sea carve may cut, so a full-strength sea is
// (NETHER_LAVA_LEVEL_Y - NETHER_BASIN_FLOOR_Y) blocks of lava deep.
#define NETHER_BASIN_FLOOR_Y    15

// Rivers cut shallower than seas: a channel reads as a winding stream
// rather than a canyon, and a shallower cut leaves more of the slab
// intact where rivers cross hills.
#define NETHER_RIVER_FLOOR_Y    17

// Lowest possible land surface, one above the lava level -- so any column
// that is not carved is dry by construction, and the shoreline of every
// sea is exactly one block of netherrack standing proud of the lava.
#define NETHER_LAND_BASE_Y      (NETHER_LAVA_LEVEL_Y + 1)   // y=21
#define NETHER_HILL_MAX_HEIGHT  7      // land tops out at y=28

#define NETHER_CEIL_BASE_Y      (NETHER_BEDROCK_TOP - 1)    // y=46
#define NETHER_CEIL_HILL_MAX    7      // ceiling bottoms out at y=39
#define NETHER_MIN_GAP          10     // guaranteed navigable air between the two layers

// Lowest y any downward surface scan needs to reach: the bottom of the
// deepest possible basin. Nothing standable exists below this.
#define NETHER_SCAN_MIN_Y       NETHER_BASIN_FLOOR_Y

// Budget check, worst case: land top 21+7 = 28; ceiling bottom 46-7 = 39;
// gap = 11, one clear of NETHER_MIN_GAP. The clamp below therefore never
// fires on ordinary terrain and exists purely as a guard for the rare
// touch-point columns' neighbours.

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
// (not chunk-local) so terrain stays continuous across chunk borders, same
// requirement the old cave-carving code had for its own tunnel math.
// Values are held in file-local statics and lazily built per world seed,
// same lifetime pattern nether_biome.cpp already uses for its own seed-
// derived state.
static bool s_noiseReady = false;
static long s_noiseForSeed = 0;
static PerlinNoise* s_floorNoise = 0;   // land relief + which columns are sea
static PerlinNoise* s_ceilNoise = 0;    // ceiling-hill depth
static PerlinNoise* s_touchNoise = 0;   // sparse field picking touch-point columns
static PerlinNoise* s_riverNoise = 0;   // ridged field tracing lava river channels

// --- Field calibration ----------------------------------------------------
// PerlinNoise::getValue halves `pow` on each octave, so octave i is
// sampled at x*pow -- progressively LOWER frequency -- and divided by pow
// -- progressively HIGHER amplitude. The most heavily weighted octave is
// therefore also the flattest one. At NETHER_HILL_NOISE_SCALE that
// dominant term varies by well under one noise unit across the entire
// 256-block strip, so in practice it acts as a per-seed constant OFFSET
// rather than as terrain variation.
//
// The previous code fought this with fixed divisors (NETHER_FLOOR_NOISE_
// GAIN and friends). A divisor cannot fix an offset: it rescales the
// spread and leaves the centre exactly where it was, which is why a
// threshold that selected a quarter of columns on one seed selected all
// of them on another.
//
// So the thresholds are not compile-time constants any more. Once per
// world, each field is sampled on a coarse grid over the strip's real
// footprint, and each threshold is taken as the QUANTILE of that sample
// for the coverage actually wanted. Asking for "the lowest 32% of columns
// are sea" yields 32% on every seed, because the number being compared
// against is derived from that seed's own distribution rather than
// guessed in advance.
//
// The knobs below are now stated as fractions of columns, which is both
// what we actually care about and directly checkable by measurement.
// Lava coverage. These are fractions of columns, and because the
// thresholds are quantile-calibrated per seed (see above) they mean what
// they say rather than approximately what they say.
//
// The sea was 0.32 and the river shore 0.055, which put open lava under
// something like 38% of the strip once the two are unioned -- the whole
// place read as an archipelago in a lava ocean rather than as caverns with
// lava in them. At 0.18 and 0.035 the union is about 1 - 0.82*0.935, so
// roughly 23%: still the dominant terrain feature, no longer the terrain.
//
// Both are independent fields, so the union really is multiplicative --
// river channels cross seas as often as chance dictates, and a river that
// happens to run through a sea costs nothing extra because netherRockTop
// takes a min() rather than deepening it.
#define NETHER_SEA_FRACTION        0.18f  // columns that are open lava sea
#define NETHER_HILL_FULL_QUANTILE  0.92f  // land value at which hills reach full height
#define NETHER_RIVER_FRACTION      0.030f // columns in a river channel proper
#define NETHER_RIVER_SHORE_EXTRA   0.035f // further columns forming the submerged shelf

// Columns beyond the shelf that form the DRY eroded bank.
//
// The shelf above is entirely below the lava level -- every column in it
// is lava, and the "slope" it makes is underneath the surface where you
// cannot see it. So the outer edge of the shelf used to butt straight up
// against whatever the hill field wanted, which across a hill meant a
// vertical netherrack wall dropping ten-odd blocks into the lava. Rivers
// looked like they had been stamped through the landscape with a cutter.
//
// This band sits outside the lava entirely and clamps the rock top down
// toward NETHER_LAND_BASE_Y at its inner edge, releasing back to the hill
// field's own height at its outer edge. It only ever lowers terrain
// (min()), so it cannot raise a bank out of a sea or fill a basin.
#define NETHER_RIVER_BANK_EXTRA    0.075f

#define NETHER_TOUCH_FRACTION      0.015f // columns allowed to become floor-to-ceiling pillars

#define NETHER_HILL_NOISE_SCALE  0.02f
#define NETHER_RIVER_NOISE_SCALE 0.010f
#define NETHER_TOUCH_NOISE_SCALE 0.008f

// Calibrated thresholds, in the fields' own raw units.
static float s_seaThreshold    = 0.0f;
static float s_seaDeepAt       = 0.0f;
static float s_hillFullAt      = 1.0f;
static float s_ceilThreshold   = 0.0f;
static float s_ceilFullAt      = 1.0f;
static float s_riverMedian     = 0.0f;
static float s_riverHalfWidth  = 0.0f;
static float s_riverShoreOuter = 0.0f;
static float s_riverBankOuter  = 0.0f;
static float s_touchThreshold  = 0.0f;

// 40x40 samples across the strip. 1600 noise evaluations per field, four
// fields, once per world -- negligible against the cost of generating even
// a single chunk, and it happens during world load rather than in play.
#define NETHER_CAL_GRID 40
static float s_calBuf[NETHER_CAL_GRID * NETHER_CAL_GRID];

static int netherCalCompare(const void* a, const void* b) {
    float fa = *(const float*)a, fb = *(const float*)b;
    return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

// Fills s_calBuf with a coarse sample of `p` across the strip, sorted
// ascending. Returns the sample count.
static int netherCalSample(PerlinNoise* p, float scale, int ox, int oz) {
    const int stripBlocks = WORLD_NETHER_CHUNKS * 16;
    int step = stripBlocks / NETHER_CAL_GRID;
    if (step < 1) step = 1;

    int n = 0;
    for (int i = 0; i < NETHER_CAL_GRID; i++)
        for (int j = 0; j < NETHER_CAL_GRID; j++) {
            float gx = (float)(ox + i * step), gz = (float)(oz + j * step);
            s_calBuf[n++] = p->getValue(gx * scale, gz * scale);
        }
    qsort(s_calBuf, n, sizeof(float), netherCalCompare);
    return n;
}

static float netherCalAt(int n, float q) {
    int i = (int)(q * (float)(n - 1) + 0.5f);
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    return s_calBuf[i];
}

static void ensureNetherNoise(long worldSeed, const World* w) {
    if (s_noiseReady && s_noiseForSeed == worldSeed) return;
    // Different XOR constants per field (and different again from
    // nether_biome.cpp's own 0x4E45544CL) so the four noise fields and the
    // biome placement are all independent of one another even though they
    // share the same world seed.
    Random rf(worldSeed ^ 0x466C6F6FL); // "Floo"r
    Random rc(worldSeed ^ 0x4365696CL); // "Ceil"
    Random rt(worldSeed ^ 0x546F7563L); // "Touc"h
    Random rr(worldSeed ^ 0x52697665L); // "Rive"r
    delete s_floorNoise; delete s_ceilNoise; delete s_touchNoise; delete s_riverNoise;
    s_floorNoise = new PerlinNoise(&rf, 4);
    s_ceilNoise  = new PerlinNoise(&rc, 4);
    s_touchNoise = new PerlinNoise(&rt, 2);
    s_riverNoise = new PerlinNoise(&rr, 3);

    // Calibrate over where this world's Nether actually is. The strip's X
    // origin depends on the preset's overworld width (worldNetherOriginCX
    // in world.h), and sampling the wrong window would calibrate against a
    // part of the noise field the player never sees.
    int ox = worldNetherOriginCX(w) * 16;
    int oz = WORLD_NETHER_ORIGIN_CZ * 16;

    int n = netherCalSample(s_floorNoise, NETHER_HILL_NOISE_SCALE, ox, oz);
    s_seaThreshold = netherCalAt(n, NETHER_SEA_FRACTION);
    // Where a sea reaches its full depth: the very bottom of the
    // distribution, so basin depth spreads across the whole sea band
    // rather than saturating immediately past the shoreline.
    s_seaDeepAt    = netherCalAt(n, 0.0f);
    s_hillFullAt   = netherCalAt(n, NETHER_HILL_FULL_QUANTILE);

    n = netherCalSample(s_ceilNoise, NETHER_HILL_NOISE_SCALE, ox, oz);
    s_ceilThreshold = netherCalAt(n, NETHER_SEA_FRACTION);
    s_ceilFullAt    = netherCalAt(n, NETHER_HILL_FULL_QUANTILE);

    // The river field is RIDGED: the feature is "close to the middle of
    // the distribution", not "low in it", so its thresholds are quantiles
    // of |value - median| rather than of the value. Taking the median
    // first and then re-sorting the absolute deviations is valid because
    // only the multiset of values matters here, not which column each came
    // from.
    n = netherCalSample(s_riverNoise, NETHER_RIVER_NOISE_SCALE, ox, oz);
    s_riverMedian = netherCalAt(n, 0.5f);
    for (int i = 0; i < n; i++) {
        float d = s_calBuf[i] - s_riverMedian;
        s_calBuf[i] = (d < 0.0f) ? -d : d;
    }
    qsort(s_calBuf, n, sizeof(float), netherCalCompare);
    s_riverHalfWidth  = netherCalAt(n, NETHER_RIVER_FRACTION);
    s_riverShoreOuter = netherCalAt(n, NETHER_RIVER_FRACTION + NETHER_RIVER_SHORE_EXTRA);
    // Outer limit of the dry eroded bank. Cumulative with the two above --
    // these are nested quantiles of the same sorted deviation buffer, so
    // each band's fraction is the EXTRA columns it adds, not its own total.
    s_riverBankOuter  = netherCalAt(n, NETHER_RIVER_FRACTION + NETHER_RIVER_SHORE_EXTRA
                                       + NETHER_RIVER_BANK_EXTRA);

    n = netherCalSample(s_touchNoise, NETHER_TOUCH_NOISE_SCALE, ox, oz);
    s_touchThreshold = netherCalAt(n, 1.0f - NETHER_TOUCH_FRACTION);

    s_noiseForSeed = worldSeed;
    s_noiseReady = true;
}

// Safe division for a calibrated span that could in principle collapse to
// zero width (a degenerate, perfectly flat noise field).
static float netherSpanT(float v, float lo, float hi) {
    float span = hi - lo;
    if (span <= 0.000001f) return (v >= hi) ? 1.0f : 0.0f;
    float t = (v - lo) / span;
    if (t < 0.0f) return 0.0f;
    if (t > 1.0f) return 1.0f;
    return t;
}

// --- Rock top -------------------------------------------------------------
// The y of the highest solid netherrack block in a column. Above
// NETHER_LAVA_LEVEL_Y this is dry land; below it, the fill loop floods
// everything up to the lava level, which is what makes a sea a sea.
//
// Note there is no "sea sentinel" height any more. The old code needed
// NETHER_SEA_FLOOR_H = -1 as a magic negative because a height of 0 still
// laid one netherrack course and capped the lava sheet underneath it. In a
// carved model the sea is simply a column whose rock top sits below the
// lava level, so the sea and the land are described by the same number and
// there is nothing to special-case.
static int netherRockTop(int gx, int gz) {
    float v = s_floorNoise->getValue(gx * NETHER_HILL_NOISE_SCALE,
                                     gz * NETHER_HILL_NOISE_SCALE);
    int top;

    if (v < s_seaThreshold) {
        // Carved basin. Depth ramps from one block at the shoreline to the
        // full basin floor at the very bottom of the distribution, so seas
        // shelve away from their edges instead of being flat-bottomed pits
        // with vertical walls.
        //
        // Starting the ramp AT the threshold (rock top = lava level - 1,
        // i.e. already one block of lava) rather than at zero depth is what
        // makes NETHER_SEA_FRACTION mean what it says: every column below
        // the threshold genuinely holds lava, so the measured coverage
        // matches the requested fraction instead of falling short of it by
        // however many columns were only shallowly dipped.
        float u = 1.0f - netherSpanT(v, s_seaDeepAt, s_seaThreshold);
        top = (NETHER_LAVA_LEVEL_Y - 1)
            - (int)(u * (float)((NETHER_LAVA_LEVEL_Y - 1) - NETHER_BASIN_FLOOR_Y) + 0.5f);
    } else {
        // Dry land. Gentle curve rather than a plain t*t: squaring pulled
        // mid-range columns below one block, flattening most of the map
        // into a plain at exactly the base height.
        float t = netherSpanT(v, s_seaThreshold, s_hillFullAt);
        float h = (0.35f + 0.65f * t) * t * (float)NETHER_HILL_MAX_HEIGHT;
        top = NETHER_LAND_BASE_Y + (int)(h + 0.5f);
    }

    // River channels cut through whatever the field wanted here, including
    // straight across hills -- min(), so a river crossing an existing sea
    // does nothing rather than deepening it into a trench.
    float r = s_riverNoise->getValue(gx * NETHER_RIVER_NOISE_SCALE,
                                     gz * NETHER_RIVER_NOISE_SCALE) - s_riverMedian;
    if (r < 0.0f) r = -r;
    if (r < s_riverShoreOuter) {
        float strength;
        if (r < s_riverHalfWidth) strength = 1.0f;
        else strength = 1.0f - netherSpanT(r, s_riverHalfWidth, s_riverShoreOuter);
        int riverTop = (NETHER_LAVA_LEVEL_Y - 1)
                     - (int)(strength * (float)((NETHER_LAVA_LEVEL_Y - 1) - NETHER_RIVER_FLOOR_Y) + 0.5f);
        if (riverTop < top) top = riverTop;
    } else if (r < s_riverBankOuter) {
        // The dry eroded bank. t runs 0 at the lava's edge to 1 where the
        // band ends, and the cap climbs from NETHER_LAND_BASE_Y (one block
        // proud of the lava) back up to a full-height hill.
        //
        // t*t rather than t on purpose: an ease-in keeps the cap low for
        // the first half of the band and does most of its climbing at the
        // outer edge, which is the shape water-cut banks actually have --
        // a wide flat terrace by the shore steepening as it leaves. Plain
        // linear gives a uniform ramp that reads as a man-made embankment.
        //
        // min() again, so this only ever cuts material away. A bank that
        // crosses a sea or another river channel leaves it alone.
        float t = netherSpanT(r, s_riverShoreOuter, s_riverBankOuter);
        int bankCap = NETHER_LAND_BASE_Y
                    + (int)(t * t * (float)NETHER_HILL_MAX_HEIGHT + 0.5f);
        if (bankCap < top) top = bankCap;
    }

    if (top < NETHER_BASIN_FLOOR_Y) top = NETHER_BASIN_FLOOR_Y;
    return top;
}

static int ceilHillDepth(int gx, int gz) {
    float v = s_ceilNoise->getValue(gx * NETHER_HILL_NOISE_SCALE,
                                    gz * NETHER_HILL_NOISE_SCALE);
    if (v < s_ceilThreshold) return 0; // flat ceiling, not a hole -- the roof stays sealed
    float t = netherSpanT(v, s_ceilThreshold, s_ceilFullAt);
    return (int)((0.35f + 0.65f * t) * t * (float)NETHER_CEIL_HILL_MAX + 0.5f);
}

// True for the rare columns chosen to be touch-point pillars, where the
// floor and ceiling are deliberately allowed to meet instead of being held
// apart by NETHER_MIN_GAP. Sparse low-frequency noise thresholded very
// high, so genuine touch points are isolated rather than forming a whole
// wall of pillars.
static bool isTouchPointColumn(int gx, int gz) {
    float v = s_touchNoise->getValue(gx * NETHER_TOUCH_NOISE_SCALE,
                                     gz * NETHER_TOUCH_NOISE_SCALE);
    return v >= s_touchThreshold;
}

// --- Bulk fill: bedrock box, netherrack slab, carved lava, ceiling hills --

static void netherFillColumn(World* w, int cx, int cz) {
    ensureNetherNoise(worldGenSeed(), w);

    int xo = cx * 16, zo = cz * 16;
    bool edgeX0 = isNetherStripEdgeX0(w, cx), edgeX1 = isNetherStripEdgeX1(w, cx);
    bool edgeZ0 = isNetherStripEdgeZ0(w, cz), edgeZ1 = isNetherStripEdgeZ1(w, cz);

    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            int gx = xo + x, gz = zo + z;

            // Side-wall bedrock: only the single outermost block-column on
            // whichever of the 4 sides this chunk actually touches.
            bool onSideWall = (edgeX0 && x == 0) || (edgeX1 && x == 15) ||
                               (edgeZ0 && z == 0) || (edgeZ1 && z == 15);
            if (onSideWall) {
                for (int y = 0; y < NETHER_H; y++) blockPut(w, gx, y, gz, BLOCK_BEDROCK);
                continue;
            }

            int rockTop = netherRockTop(gx, gz);
            int ceilH   = ceilHillDepth(gx, gz);

            if (!isTouchPointColumn(gx, gz)) {
                // Hold the two layers apart by at least NETHER_MIN_GAP. Only
                // the ceiling is ever shortened here: trimming the floor
                // instead could drop a land column below the lava level and
                // silently flood it, turning "this cavern was a bit tight"
                // into "there is a lake here", which is exactly the kind of
                // coupling the carved model exists to remove.
                int ceilBottomY = NETHER_CEIL_BASE_Y - ceilH;
                int gap = ceilBottomY - rockTop;
                if (gap < NETHER_MIN_GAP) {
                    int deficit = NETHER_MIN_GAP - gap;
                    ceilH = (ceilH - deficit < 0) ? 0 : ceilH - deficit;
                }
            }
            // touch==true columns skip the clamp entirely -- their floor and
            // ceiling heights are used as-is, and since both noise fields
            // tend toward their extremes near the same low-frequency peaks
            // the touch field selects, they naturally meet or nearly meet
            // without either height needing to be force-inflated.

            int ceilBottomY = NETHER_CEIL_BASE_Y - ceilH;

            for (int y = 0; y < NETHER_H; y++) {
                unsigned char id;
                if (y == NETHER_BEDROCK_BOTTOM || y == NETHER_BEDROCK_TOP) {
                    id = BLOCK_BEDROCK;
                } else if (y <= rockTop) {
                    id = BLOCK_NETHERRACK;      // the slab, whatever survived the carve
                } else if (y <= NETHER_LAVA_LEVEL_Y) {
                    id = BLOCK_CALM_LAVA;       // carved basin, flooded to the lava level
                } else if (y >= ceilBottomY) {
                    id = BLOCK_NETHERRACK;      // ceiling hill
                } else {
                    id = BLOCK_AIR;             // navigable gap
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

// --- Wastes structure: netherrack spire -----------------------------
//
// A thin rock column jutting up from the floor, tapering slightly near
// the top and optionally capped with magma or glowstone. This is purely
// ambient decoration for the otherwise-empty Nether Wastes biome -- no
// footprint validation or chunk-rejection the way the fortress/dungeon
// generators need, since a spire that fails its headroom check simply
// isn't placed and the biome falls back to its existing flat terrain,
// which was already the status quo.
//
// Height is capped conservatively (5-8 blocks) against NETHER_MIN_GAP
// (10): even the tallest possible spire leaves at least 2 blocks of
// clearance below the lowest the ceiling can ever sit relative to a
// floor column directly below it, so there's no need to re-derive the
// ceiling height here -- the local headroom scan below is the real
// authority and the height cap is just a sane upper bound to roll from.
static void placeNetherrackSpire(World* w, Random& random, int x, int y, int z) {
    if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) return;
    if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) return;

    int wantHeight = 5 + random.nextInt(4);
    int headroom = 0;
    while (headroom < wantHeight && worldBlock(w, x, y + 1 + headroom, z) == BLOCK_AIR) headroom++;
    int height = headroom < wantHeight ? headroom : wantHeight;
    if (height < 3) return; // too little clearance to read as a spire; skip rather than stub

    for (int h = 1; h <= height; h++) {
        // Tapers near the top: the last two blocks have a chance to skip,
        // giving an irregular jagged silhouette instead of a perfect rod.
        if (h > height - 2 && random.nextInt(3) == 0) continue;
        blockPut(w, x, y + h, z, BLOCK_NETHERRACK);
    }

    // Occasional accent cap -- magma reads as a scorched tip, glowstone
    // as a light source that also helps the spire read from a distance.
    // Never both; capChance keeps most spires plain rock, matching how
    // sparse the existing magma-blob decoration already is in this biome.
    int capRoll = random.nextInt(6);
    if (capRoll == 0) blockPut(w, x, y + height + 1, z, BLOCK_MAGMA);
    else if (capRoll == 1) blockPut(w, x, y + height + 1, z, BLOCK_GLOWSTONE);
}

// --- Wastes structure: small obsidian formation -----------------------
//
// A squat, irregular blob of obsidian sitting on the floor -- reads as a
// natural rock outcrop, distinct from the thin vertical spires above.
// Obsidian only forms where lava meets a stable surface in vanilla; this
// is a decorative stand-in for that without simulating the actual fluid
// interaction, matching how the existing magma blobs in this file are
// already "purely decorative...no lava-damage/bubble-column behavior".
static void placeObsidianFormation(World* w, Random& random, int x, int y, int z) {
    if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) return;
    if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) return;

    int radius = 1 + random.nextInt(2);
    int height = 1 + random.nextInt(2);
    for (int dx = -radius; dx <= radius; dx++) {
        for (int dz = -radius; dz <= radius; dz++) {
            if (dx * dx + dz * dz > radius * radius) continue; // round footprint, not square
            for (int dy = 0; dy < height; dy++) {
                int bx = x + dx, by = y + 1 + dy, bz = z + dz;
                if (worldBlock(w, bx, by, bz) != BLOCK_AIR) continue;
                // Only builds on solid ground directly below -- an
                // obsidian blob floating a block above an already-placed
                // neighbour column would look like debris, not an
                // outcrop, so each column is checked independently.
                if (worldBlock(w, bx, by - 1, bz) == BLOCK_AIR) continue;
                blockPut(w, bx, by, bz, BLOCK_OBSIDIAN);
            }
        }
    }
}

// --- Soul Sand Valley structure: fossil skeleton -----------------------
//
// A buried skeleton -- a spine with rib pairs, echoing vanilla's own
// fossil structures -- rather than a random scatter of bone blocks. Fixed
// orientation with an optional mirror across Z (cheap variety without a
// full rotation matrix, matching how little investment the fortress and
// dungeon generators put into orientation too). The rib layout is
// deliberately asymmetric (one pair shortened on one side, another
// dropped on one side entirely) so the mirror actually produces a
// different-looking skeleton rather than mapping onto itself -- a fully
// symmetric rib layout was tried first and rejected because mirroring it
// is a no-op, confirmed by generating both orientations as coordinate
// sets and checking they differ. The template is a flat coordinate list
// rather than ported vanilla structure data, hand-authored to read as a
// spine + ribcage from directly above -- see the ASCII check this shape
// was verified against before being written out here:
//
//   NORMAL:            MIRRORED:
//    # #     #          #    #  #
//    # #     #          # #  #  #
//    ###########         ###########
//    # #  #  #          # #     #
//    #    #  #          # #     #
//
// 11 long (x), 5 wide (z), 3 tall (y) -- comparable footprint to vanilla's
// real fossils. y within the template is a shallow arc (0->2->0) so the
// spine reads as gently curved rather than a dead-straight line of blocks.
struct FossilBlock { signed char dx, dy, dz; };
static const FossilBlock kFossilTemplate[] = {
    // Spine (11 vertebrae, shallow arc).
    {0,0,0}, {1,0,0}, {2,1,0}, {3,1,0}, {4,1,0}, {5,2,0}, {6,2,0},
    {7,1,0}, {8,1,0}, {9,0,0}, {10,0,0},
    // Rib pairs, at the same height as the spine vertebra directly below
    // them. Deliberately asymmetric -- see the comment above.
    {1,0,1}, {1,0,2}, {1,0,-1}, {1,0,-2},   // symmetric pair
    {3,1,1},                                 // +Z side shortened to 1 block
    {3,1,-1}, {3,1,-2},
    {6,2,1}, {6,2,2},                        // -Z side dropped entirely
    {9,0,1}, {9,0,2}, {9,0,-1}, {9,0,-2},   // symmetric pair
};
static const int kFossilBlockCount = (int)(sizeof(kFossilTemplate) / sizeof(kFossilTemplate[0]));

// True if every template cell either lands on soul sand/soil (the anchor
// column and every other column the skeleton would occupy) with open air
// immediately above, or -- since the whole thing is meant to sit half
// buried -- one block INTO existing soul sand/soil is also acceptable,
// which the placement loop below relies on to let ribs dip half a block
// under the surface. Checked as a dry run before anything is placed, same
// footprint-first discipline as the dungeon/fortress generators use,
// rather than discovering a bad site block-by-block mid-placement.
static bool fossilSiteClear(World* w, int x, int y, int z, bool mirrorZ) {
    for (int i = 0; i < kFossilBlockCount; i++) {
        int dz = mirrorZ ? -kFossilTemplate[i].dz : kFossilTemplate[i].dz;
        int bx = x + kFossilTemplate[i].dx;
        int by = y + kFossilTemplate[i].dy;
        int bz = z + dz;
        unsigned char at    = worldBlock(w, bx, by, bz);
        unsigned char below = worldBlock(w, bx, by - 1, bz);
        bool atOk    = (at == BLOCK_AIR || at == BLOCK_SOUL_SAND || at == BLOCK_SOUL_SOIL);
        bool belowOk = (below == BLOCK_SOUL_SAND || below == BLOCK_SOUL_SOIL || below == BLOCK_AIR);
        if (!atOk || !belowOk) return false;
    }
    return true;
}

static void placeFossilSkeleton(World* w, Random& random, int x, int y, int z) {
    unsigned char floorId = worldBlock(w, x, y, z);
    if (floorId != BLOCK_SOUL_SAND && floorId != BLOCK_SOUL_SOIL) return;
    if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) return;

    bool mirrorZ = random.nextInt(2) == 0;
    if (!fossilSiteClear(w, x, y, z, mirrorZ)) return;

    for (int i = 0; i < kFossilBlockCount; i++) {
        int dz = mirrorZ ? -kFossilTemplate[i].dz : kFossilTemplate[i].dz;
        int bx = x + kFossilTemplate[i].dx;
        int by = y + kFossilTemplate[i].dy;
        int bz = z + dz;
        blockPut(w, bx, by, bz, BLOCK_BONE_BLOCK);
    }
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
        // Shoreline band. Was NETHER_LAVA_FLOOR_TOP + 1 + rand(3), which
        // tracked the old permanent lava sheet at the very bottom of the
        // world; with lava now pooled at NETHER_LAVA_LEVEL_Y, the strip of
        // netherrack just proud of the lava is where magma belongs.
        int y = NETHER_LAVA_LEVEL_Y + 1 + random.nextInt(2);
        if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) continue;
        if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
        int blobSize = 2 + random.nextInt(4);
        for (int b = 0; b < blobSize; b++) {
            int bx = x + random.nextInt(3) - 1, bz = z + random.nextInt(3) - 1;
            if (worldBlock(w, bx, y, bz) == BLOCK_NETHERRACK)
                blockPut(w, bx, y, bz, BLOCK_MAGMA);
        }
    }

    // Spires and obsidian formations: the actual "give Wastes some
    // structure" pass. One roll each per chunk, independent of the magma
    // above -- both search for the topmost exposed floor column the same
    // way decorateSoulSandValley already does for its floor material,
    // since spires/formations need to sit on *any* floor surface, not
    // specifically the narrow lava-shoreline band magma targets.
    if (random.nextInt(3) == 0) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_SCAN_MIN_Y; y--) {
            if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) continue;
            if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
            placeNetherrackSpire(w, random, x, y, z);
            break;
        }
    }
    if (random.nextInt(4) == 0) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_SCAN_MIN_Y; y--) {
            if (worldBlock(w, x, y, z) != BLOCK_NETHERRACK) continue;
            if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
            placeObsidianFormation(w, random, x, y, z);
            break;
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
            for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_SCAN_MIN_Y; y--) {
                if (worldBlock(w, gx, y, gz) != BLOCK_NETHERRACK) continue;
                if (worldBlock(w, gx, y + 1, gz) != BLOCK_AIR) continue;
                // Top-of-floor netherrack exposed to an open pocket above it.
                unsigned char floorBlock = (random.nextInt(4) == 0) ? BLOCK_SOUL_SOIL : BLOCK_SOUL_SAND;
                blockPut(w, gx, y, gz, floorBlock);
                break; // only the topmost exposed layer at this column
            }
        }
    }

    // Fossil skeleton: placed after the floor conversion above so the
    // soul sand/soil surface it checks for already exists. One roll per
    // chunk at low odds -- unlike the old scatter-blob version, this is
    // an 11x5 footprint that needs genuinely clear, flat soul sand under
    // it, so most rolls will fail fossilSiteClear's check anyway; the low
    // roll rate just avoids spending the check on every single chunk.
    // Anchor x is kept within [xo, xo+5] rather than the full [xo,xo+16)
    // a single-block feature would use, since the template extends 10
    // blocks further in +X from its anchor and reaching past this
    // chunk's own 16-wide footprint into a neighbour that may not be
    // generated yet would corrupt worldBlock reads there.
    if (random.nextInt(4) == 0) {
        int x = xo + random.nextInt(6), z = zo + random.nextInt(16);
        for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_SCAN_MIN_Y; y--) {
            unsigned char id = worldBlock(w, x, y, z);
            if (id != BLOCK_SOUL_SAND && id != BLOCK_SOUL_SOIL) continue;
            if (worldBlock(w, x, y + 1, z) != BLOCK_AIR) continue;
            placeFossilSkeleton(w, random, x, y, z);
            break;
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
    for (int y = NETHER_CEIL_BASE_Y; y > NETHER_SCAN_MIN_Y; y--) {
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

// --- Warped Forest density -----------------------------------------------
// These four numbers are the whole answer to "the warped trees are too
// frequent". They were tuned when both tree scales were mostly failing to
// place: growHugeWarpedFungus was rolling heights taller than the entire
// navigable gap, and both placers were picking a random y in the 34-block
// span and requiring it to already be the block sitting on nylium, so the
// overwhelming majority of attempts silently did nothing. The counts were
// pushed up to compensate for that waste.
//
// Both of those bugs were then fixed (findNyliumSurface searches down for
// the real surface, and the fungus height is clamped to actual headroom),
// which turned nearly every attempt into a successful placement -- and the
// inflated counts, which had been producing a reasonable-looking forest by
// accident, suddenly produced roughly four times as many structures as
// intended. Halving the tree attempts and cutting the mega fungi to a
// quarter puts real placed counts back where the original numbers were
// aiming.
#define NETHER_HUGE_FUNGUS_TRIES_MIN   2   // was 8
#define NETHER_HUGE_FUNGUS_TRIES_VAR   3   // was 8
#define NETHER_WARPED_TREE_TRIES_MIN   6   // was 14
#define NETHER_WARPED_TREE_TRIES_VAR   5   // was 10

static void decorateWarpedForest(World* w, Random& random, int xo, int zo) {
    // Real warped nylium/wart block/fungus/roots/sprouts now exist as
    // block ids (see chunk.h) -- this replaces the earlier wool-color and
    // mushroom-feature placeholders from the first pass.
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            int gx = xo + x, gz = zo + z;
            for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_SCAN_MIN_Y; y--) {
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
        int y = NETHER_SCAN_MIN_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_SCAN_MIN_Y);
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
    int hugeFungusTries = NETHER_HUGE_FUNGUS_TRIES_MIN
                        + random.nextInt(NETHER_HUGE_FUNGUS_TRIES_VAR);
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
    int treeTries = NETHER_WARPED_TREE_TRIES_MIN
                  + random.nextInt(NETHER_WARPED_TREE_TRIES_VAR);
    for (int i = 0; i < treeTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y;
        if (!findNyliumSurface(w, x, z, &y)) continue;
        growWarpedTree(w, random, x, y, z);
    }

    int wartTries = 2 + random.nextInt(3);
    for (int i = 0; i < wartTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_SCAN_MIN_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_SCAN_MIN_Y);
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
        int y = NETHER_SCAN_MIN_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_SCAN_MIN_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_ROOTS);
    }

    int sproutTries = 2 + random.nextInt(3);
    for (int i = 0; i < sproutTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_SCAN_MIN_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_SCAN_MIN_Y);
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
        int y = NETHER_SCAN_MIN_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_SCAN_MIN_Y);
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
    for (int y = NETHER_CEIL_BASE_Y; y > NETHER_SCAN_MIN_Y; y--) {
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (!isNetherrackFace(w, x, y + 1, z)) continue;
        *outY = y;
        return true;
    }
    return false;
}

// Glowstone density. 8-15 clusters per chunk of 5-11 blocks each is
// 40-165 glowstone blocks in every single 16x16 -- the ceiling was
// effectively paved with it, and the Nether was lit like a supermarket.
// Same story as the warped trees above: the count was raised while
// findCeilingFaceSpot did not yet exist and the placer was picking a
// random y and demanding it already be the ceiling face, so almost every
// cluster silently failed. With the face search landing every attempt,
// the raised count became the real count.
//
// 1-3 clusters of 4-9 blocks reads as occasional glowing patches on the
// roof, which is both what vanilla looks like and what makes the stuff
// worth mining for.
#define NETHER_GLOWSTONE_CLUSTERS_MIN 1   // was 8
#define NETHER_GLOWSTONE_CLUSTERS_VAR 3   // was 8
#define NETHER_GLOWSTONE_BLOB_MIN     4   // was 5
#define NETHER_GLOWSTONE_BLOB_VAR     6   // was 7

static void placeCeilingGlowstone(World* w, int xo, int zo, Random& random) {
    // Applies to every biome (not just Wastes) -- glowstone clusters are a
    // blanket ambient feature of the whole Nether.
    int clusters = NETHER_GLOWSTONE_CLUSTERS_MIN
                 + random.nextInt(NETHER_GLOWSTONE_CLUSTERS_VAR);
    for (int i = 0; i < clusters; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y;
        if (!findCeilingFaceSpot(w, x, z, &y)) continue;

        // Clusters hang a little way down from the face rather than being
        // a flat one-block sheet, so they read as blobs from below.
        int blobSize = NETHER_GLOWSTONE_BLOB_MIN + random.nextInt(NETHER_GLOWSTONE_BLOB_VAR);
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
// Falls now only ever appear OVER LAVA. Both placers pick a column that is
// already open sea or river and work upward from it, rather than picking a
// spot anywhere and pouring until something stops the column. Two things
// fall out of that:
//
//   * The old drop cap is gone as a tuning value. It was 26, sized for the
//     original 100-tall shell and never rescaled when the shell came down
//     to 40, which is precisely why falls hung in mid-air: a ceiling face
//     sat near y=37 and the exposed lava was at y=3, a drop of 34, so the
//     cap fired at y=11 and left the column stopping eight blocks above
//     anything. It survives only as a runaway guard, derived from the box
//     height so it cannot go stale again.
//
//   * The splash-pool code is gone entirely. It existed to give a fall
//     landing on dry netherrack something to feed, by digging a shallow
//     basin under it and flooding that. A fall that by construction lands
//     in a lava sea already terminates in lava at the shared lava level,
//     so digging a private pool for it would only carve a dent in a
//     surface that is already the right height.
#define NETHER_LAVAFALL_MAX_DROP NETHER_H
#define NETHER_LAVAFALL_MIN_DROP 4

// Is this column open lava at the shared lava level? That is the whole
// definition of "sea or river" in the carved model -- a column is flooded
// exactly when its rock top fell below NETHER_LAVA_LEVEL_Y, and decoration
// never writes at or below that level (it only ever targets netherrack
// with open air above it), so this stays true after decoration has run.
static bool netherColumnIsLava(World* w, int x, int z) {
    return worldBlock(w, x, NETHER_LAVA_LEVEL_Y, z) == BLOCK_CALM_LAVA;
}

// Pours a column of lava down from yTop. Returns false without writing
// anything if the drop would be too short to read as a fall -- measuring
// first means a rejected fall costs nothing to unwind.
static bool pourLavafall(World* w, int x, int yTop, int z) {
    int yLand = yTop;
    while (yLand > NETHER_LAVA_LEVEL_Y && worldBlock(w, x, yLand, z) == BLOCK_AIR) {
        if (yTop - yLand >= NETHER_LAVAFALL_MAX_DROP) break;
        yLand--;
    }
    if (yTop - yLand < NETHER_LAVAFALL_MIN_DROP) return false;

    for (int y = yTop; y > yLand; y--)
        setBlock(w, x, y, z, BLOCK_CALM_LAVA);
    return true;
}

// One fall roughly every six chunks, rather than the old average of one
// per chunk (nextInt(3), mean 1). A feature that appears in every chunk is
// not a feature; it is terrain. Restricting falls to lava columns thins
// them further on its own, since only about a third of columns qualify.
#define NETHER_CEILING_FALL_ODDS 6

static void placeCeilingLavafalls(World* w, int xo, int zo, Random& random) {
    if (random.nextInt(NETHER_CEILING_FALL_ODDS) != 0) return;

    // A handful of tries for ONE fall, rather than several independent
    // falls: a rejected spot (too short a drop, no ceiling face) should
    // cost another look at this chunk, not silently reduce the rate.
    // More tries than before, because most candidates are now rejected for
    // being over dry land: eight looks give a lava chunk a fair chance of
    // finding its spot, while a chunk with no lava in it at all simply
    // produces nothing however many times it looks.
    for (int t = 0; t < 8; t++) {
        int x = xo + 1 + random.nextInt(14), z = zo + 1 + random.nextInt(14);
        if (!netherColumnIsLava(w, x, z)) continue;   // falls only over lava
        int y;
        if (!findCeilingFaceSpot(w, x, z, &y)) continue;
        if (!pourLavafall(w, x, y, z)) continue;
        // Set the source block into the ceiling itself so the fall reads as
        // pouring OUT of the rock rather than starting in mid-air.
        setBlock(w, x, y + 1, z, BLOCK_CALM_LAVA);
        return;
    }
}

#define NETHER_HILLSIDE_FALL_ODDS 8

static void placeHillsideLavafalls(World* w, int xo, int zo, Random& random) {
    // Lava breaking out of the side of a floor hill and running down it.
    // Needs a hill top with a genuine drop beside it, so this looks for an
    // exposed netherrack surface with an air neighbour that also has air
    // below -- the lip of a slope, not the middle of a plateau.
    static const int dx4[4] = { 1, -1, 0, 0 };
    static const int dz4[4] = { 0, 0, 1, -1 };

    // Was an unconditional 3-5 attempts in every chunk, on top of the
    // ceiling falls above. Now a minority of chunks get a hillside
    // breakout at all, and those that do get one.
    if (random.nextInt(NETHER_HILLSIDE_FALL_ODDS) != 0) return;

    int tries = 4;
    for (int t = 0; t < tries; t++) {
        int x = xo + 1 + random.nextInt(14), z = zo + 1 + random.nextInt(14);
        int y;
        // Reuse the floor-surface search, but require real elevation: a
        // breakout at the waterline of the lava sea would be invisible.
        bool found = false;
        for (y = NETHER_CEIL_BASE_Y; y >= NETHER_LAND_BASE_Y + 2; y--) {
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
        // ...and the drop must be into lava. This is the "only over a lava
        // sea" rule for hillside breakouts: the classic look is lava
        // spilling off a shore cliff into the sea below it, not a fall
        // running down a hillside onto more dry rock.
        if (!netherColumnIsLava(w, nx, nz)) continue;

        if (!pourLavafall(w, nx, y, nz)) continue;
        setBlock(w, x, y, z, BLOCK_CALM_LAVA); // the source, embedded in the hill face
        return;
    }
}

static bool findFloorSurfaceSpot(World* w, int xo, int zo, Random& random, int tries, int* outX, int* outY, int* outZ) {
    for (int t = 0; t < tries; t++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        for (int y = NETHER_CEIL_BASE_Y; y >= NETHER_SCAN_MIN_Y; y--) {
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

// --- Portal site search ---------------------------------------------------
// See the contract in nether_gen.h. This lives here rather than in
// nether_portal.cpp because it is the shell's own geometry that decides
// what "a good spot" means, and both the real portal and the debug
// teleport need the same answer.

// Lowest y a mob or player can stand on. Not the bottom of the terrain:
// every column whose rock top falls below NETHER_LAVA_LEVEL_Y is flooded
// to that level by construction, so nothing below NETHER_LAND_BASE_Y is
// ever standable and probing down there is wasted effort.
int netherShellFloorBaseY(void) { return NETHER_LAND_BASE_Y; }
int netherShellCeilBaseY(void)  { return NETHER_CEIL_BASE_Y; }

// The frame is 4 columns wide (interior 2, plus a post either side) and 5
// tall (interior 3, plus floor and ceiling courses). Offsets are relative
// to the bottom-interior block the search reports.
#define PORTAL_SITE_DX0      (-1)
#define PORTAL_SITE_DX1      ( 2)
#define PORTAL_SITE_CLEAR_H  ( 5)  // interior 3 + top course + one block of headroom
#define PORTAL_SITE_LAVA_MARGIN 2  // how far around the footprint must be lava-free

// Surface height cache for the searched region: the y of the topmost
// non-air block in the shell interior, or -1 for a column with no floor
// at all (open lava sea reaching the roof is impossible, but a column can
// be solid all the way up at a touch-point pillar).
//
// File-scope rather than a local: the searched region is up to 5x5 chunks
// of 16x16 columns, and 6400 shorts is far too much to put on a PSP thread
// stack. This is only ever touched by netherFindPortalSite, which runs a
// handful of times per save at most.
#define PORTAL_SITE_MAX_CHUNK_R 2
#define PORTAL_SITE_SPAN ((2 * PORTAL_SITE_MAX_CHUNK_R + 1) * 16)
static short s_siteSurface[PORTAL_SITE_SPAN * PORTAL_SITE_SPAN];

// Blocks a frame may legitimately stand on. Deliberately a whitelist and
// not "anything solid": glowstone, warped stem/wart and the huge fungi
// are all solid, and standing a portal on top of a tree is exactly the
// kind of result the old carve-a-platform approach was hiding.
static bool netherSiteGroundOk(unsigned char id) {
    return id == BLOCK_NETHERRACK || id == BLOCK_WARPED_NYLIUM ||
           id == BLOCK_SOUL_SAND  || id == BLOCK_SOUL_SOIL ||
           id == BLOCK_NETHER_QUARTZ_ORE;
}

// A ceiling hill also presents "solid with air above" when scanned from
// the top down, so the raw scan above can report the underside of the roof
// as a floor. Walk on down past any hanging mass to the first solid block
// that has genuine open space above it and more solid beneath it.
static bool netherSiteFloorAt(World* w, int gx, int gz, int* outY) {
    int y = NETHER_CEIL_BASE_Y;
    // Skip the ceiling hill, if this column has one.
    while (y >= NETHER_SCAN_MIN_Y && worldBlock(w, gx, y, gz) != BLOCK_AIR) y--;
    // Now fall through the open gap to the floor.
    while (y >= NETHER_SCAN_MIN_Y && worldBlock(w, gx, y, gz) == BLOCK_AIR) y--;
    if (y < NETHER_SCAN_MIN_Y) return false;
    if (isLavaId(worldBlock(w, gx, y, gz))) return false;
    *outY = y;
    return true;
}

static bool netherSiteNoLavaNear(World* w, int bx, int by, int bz) {
    const int M = PORTAL_SITE_LAVA_MARGIN;
    for (int dx = PORTAL_SITE_DX0 - M; dx <= PORTAL_SITE_DX1 + M; dx++)
    for (int dz = -M; dz <= M; dz++)
    for (int dy = -1; dy <= 1; dy++)
        if (isLavaId(worldBlock(w, bx + dx, by + dy, bz + dz))) return false;
    return true;
}

// Is (bx, by, bz) somewhere the player can stand: feet block and the block
// above both clear, solid ground underneath.
static bool netherSiteStandable(World* w, int bx, int by, int bz) {
    if (worldBlock(w, bx, by, bz) != BLOCK_AIR) return false;
    if (worldBlock(w, bx, by + 1, bz) != BLOCK_AIR) return false;
    return netherSiteGroundOk(worldBlock(w, bx, by - 1, bz));
}

// Full test for one candidate. `strict` adds the lava-margin and
// step-out-space requirements; the relaxed pass drops them so a Nether
// that genuinely has no ideal site still yields a workable one rather
// than nothing at all.
static bool netherSiteOk(World* w, int bx, int bz, int surfY, bool strict) {
    int by = surfY + 1; // bottom interior / feet level

    // The frame's ceiling course sits at by+3 and needs a block of
    // headroom above it, so the column must stay inside the shell.
    if (by + PORTAL_SITE_CLEAR_H > NETHER_CEIL_BASE_Y) return false;

    for (int dx = PORTAL_SITE_DX0; dx <= PORTAL_SITE_DX1; dx++) {
        int gx = bx + dx;
        // Flat: every frame column must sit on the same surface height,
        // so the obsidian floor course is bedded in ground rather than
        // floating over a drop on one side.
        int cs;
        if (!netherSiteFloorAt(w, gx, bz, &cs)) return false;
        if (cs != surfY) return false;
        if (!netherSiteGroundOk(worldBlock(w, gx, surfY, bz))) return false;

        for (int dy = 0; dy < PORTAL_SITE_CLEAR_H; dy++)
            if (worldBlock(w, gx, by + dy, bz) != BLOCK_AIR) return false;
    }

    if (!strict) return true;

    if (!netherSiteNoLavaNear(w, bx, by, bz)) return false;

    // At least one face has room to step out onto, across both interior
    // columns, so arriving players are not walled in against the frame.
    bool front = netherSiteStandable(w, bx, by, bz - 1) &&
                 netherSiteStandable(w, bx + 1, by, bz - 1);
    bool back  = netherSiteStandable(w, bx, by, bz + 1) &&
                 netherSiteStandable(w, bx + 1, by, bz + 1);
    return front || back;
}

bool netherFindPortalSite(World* w, int cxCentre, int czCentre, int searchChunkR,
                          int* outX, int* outY, int* outZ) {
    if (searchChunkR < 0) searchChunkR = 0;
    if (searchChunkR > PORTAL_SITE_MAX_CHUNK_R) searchChunkR = PORTAL_SITE_MAX_CHUNK_R;

    int span = (2 * searchChunkR + 1) * 16;
    int ox = (cxCentre - searchChunkR) * 16;
    int oz = (czCentre - searchChunkR) * 16;

    for (int i = 0; i < span * span; i++) s_siteSurface[i] = -1;
    for (int lx = 0; lx < span; lx++)
    for (int lz = 0; lz < span; lz++) {
        int y;
        if (netherSiteFloorAt(w, ox + lx, oz + lz, &y)) s_siteSurface[lx * span + lz] = (short)y;
    }

    // Two passes over the same candidate ordering: take the best-quality
    // site anywhere in range before settling for a merely workable one.
    for (int pass = 0; pass < 2; pass++) {
        bool strict = (pass == 0);

        // Candidates are visited in rings out from the centre of the
        // region, so the portal lands as close to the canonical spot as
        // the terrain allows instead of wherever the scan order happened
        // to reach first.
        int mid = span / 2;
        for (int ring = 0; ring <= mid; ring++) {
            for (int lx = mid - ring; lx <= mid + ring; lx++) {
                if (lx < 0 || lx >= span) continue;
                for (int lz = mid - ring; lz <= mid + ring; lz++) {
                    if (lz < 0 || lz >= span) continue;
                    // Only the ring's boundary; the interior was covered
                    // by a previous, smaller ring.
                    int adx = lx - mid, adz = lz - mid;
                    if (adx < 0) adx = -adx;
                    if (adz < 0) adz = -adz;
                    if (adx != ring && adz != ring) continue;

                    short surfY = s_siteSurface[lx * span + lz];
                    if (surfY < 0) continue;

                    int bx = ox + lx, bz = oz + lz;
                    // Keep the whole footprint and its margin inside the
                    // scanned region, so neighbouring ungenerated chunks
                    // are never read.
                    if (lx + PORTAL_SITE_DX0 - PORTAL_SITE_LAVA_MARGIN < 0) continue;
                    if (lx + PORTAL_SITE_DX1 + PORTAL_SITE_LAVA_MARGIN >= span) continue;
                    if (lz - PORTAL_SITE_LAVA_MARGIN < 0) continue;
                    if (lz + PORTAL_SITE_LAVA_MARGIN >= span) continue;

                    if (!netherSiteOk(w, bx, bz, surfY, strict)) continue;

                    *outX = bx;
                    *outY = surfY + 1;
                    *outZ = bz;
                    return true;
                }
            }
        }
    }
    return false;
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
            int x = xo + random.nextInt(16), y = NETHER_SCAN_MIN_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_SCAN_MIN_Y), z = zo + random.nextInt(16);
            netherOreFeature(w, random, x, y, z, BLOCK_NETHER_QUARTZ_ORE, 12);
        }
    }

    // Structures are generated last so terrain/decorations are already in
    // place and the fortress can deliberately replace the blocks in its
    // footprint. The generator is chunk-local for PSP streaming safety.
    if (genFeatureEnabled(worldGenMask(), GEN_FEATURE_NETHER_FORTRESS))
        netherFortressGenerateChunk(w, worldSeed, cx, cz);
}

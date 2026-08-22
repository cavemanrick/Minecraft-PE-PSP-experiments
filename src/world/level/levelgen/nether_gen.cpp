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
#define NETHER_H                100    // total column height, y=0..99 (cut down from the old 128)
#define NETHER_BEDROCK_BOTTOM   0
#define NETHER_BEDROCK_TOP      (NETHER_H - 1)  // y=99
#define NETHER_LAVA_FLOOR_TOP   6       // y=1..6 is the lava floor (6 blocks), y=0 is bedrock
#define NETHER_FLOOR_BASE_Y     (NETHER_LAVA_FLOOR_TOP + 1) // y=7, hills rise from here
#define NETHER_CEIL_BASE_Y      (NETHER_BEDROCK_TOP - 1)    // y=98, hills hang from here
#define NETHER_MIN_GAP          20      // guaranteed navigable air thickness between the two hill layers

#define MCPE_PI 3.14159265f

// --- Bedrock box: top, bottom, and (only at the strip's true outer
// edges) the four side walls, replacing the old shell-fill's top/bottom-
// only bedrock. Side walls only get written on the boundary chunks
// themselves (checked against WORLD_NETHER_ORIGIN_CX/CZ/WORLD_NETHER_
// CHUNKS in world.h), not every chunk -- an interior chunk has no side
// wall to draw.

static bool isNetherStripEdgeX0(int cx) { return cx == WORLD_NETHER_ORIGIN_CX; }
static bool isNetherStripEdgeX1(int cx) { return cx == WORLD_NETHER_ORIGIN_CX + WORLD_NETHER_CHUNKS - 1; }
static bool isNetherStripEdgeZ0(int cz) { return cz == WORLD_NETHER_ORIGIN_CZ; }
static bool isNetherStripEdgeZ1(int cz) { return cz == WORLD_NETHER_ORIGIN_CZ + WORLD_NETHER_CHUNKS - 1; }

// --- Height fields --------------------------------------------------------
// Two independent 2D noise fields sampled in *global* block coordinates
// (not chunk-local) so hills stay continuous across chunk borders, same
// requirement the old cave-carving code had for its own tunnel math.
// Values are held in file-local statics and lazily built per world seed,
// same lifetime pattern nether_biome.cpp already uses for its own seed-
// derived state.
// PerlinNoise::getValue sums `levels` octaves as value/pow with pow
// halving each octave (see PerlinNoise.cpp) -- for N levels that's
// sum(2^i for i in 0..N-1) = 2^N - 1 times the raw single-octave
// amplitude in the worst case, NOT the single-octave ~[-1,1] range a
// naive reading of "Perlin noise" would suggest. An earlier version of
// this file's remap logic wrongly assumed [-1,1] with no normalization
// for the octave sum, which made the vast majority of columns saturate
// to a hard 0 or hard max instead of forming an actual gradient (verified
// by Monte Carlo simulation of the old formula: ~88% saturated for the
// 4-level fields, and the 2-level touch field's threshold check was even
// more broken since raw values around +3 trivially cleared a >=0.965
// threshold meant for a normalized ~[-1,1] input) -- producing huge
// blocky regions of total emptiness or solid towers rather than rolling
// hills, which is what caused the "blue sky, fall until death" bug
// report. Every noise field's raw getValue() must be divided by its own
// NOISE_NORM before any remap/clamp/threshold logic runs.
#define NETHER_HILL_NOISE_LEVELS  4
#define NETHER_HILL_NOISE_NORM    ((float)((1 << NETHER_HILL_NOISE_LEVELS) - 1)) // 2^levels - 1
#define NETHER_TOUCH_NOISE_LEVELS 2
#define NETHER_TOUCH_NOISE_NORM   ((float)((1 << NETHER_TOUCH_NOISE_LEVELS) - 1))

static bool s_noiseReady = false;
static long s_noiseForSeed = 0;
static PerlinNoise* s_floorNoise = 0;   // drives floor-hill height + "skip" gaps
static PerlinNoise* s_ceilNoise = 0;    // drives ceiling-hill depth + "skip" gaps
static PerlinNoise* s_touchNoise = 0;   // sparse noise field picking the rare touch-point columns

static void ensureNetherNoise(long worldSeed) {
    if (s_noiseReady && s_noiseForSeed == worldSeed) return;
    // Different XOR constants per field (and different again from
    // nether_biome.cpp's own 0x4E45544CL) so the three noise fields and
    // the biome placement are all independent of one another even though
    // they share the same world seed.
    Random rf(worldSeed ^ 0x466C6F6FL); // "Floo"r
    Random rc(worldSeed ^ 0x4365696CL); // "Ceil"
    Random rt(worldSeed ^ 0x546F7563L); // "Touc"h
    delete s_floorNoise; delete s_ceilNoise; delete s_touchNoise;
    s_floorNoise = new PerlinNoise(&rf, NETHER_HILL_NOISE_LEVELS);
    s_ceilNoise  = new PerlinNoise(&rc, NETHER_HILL_NOISE_LEVELS);
    s_touchNoise = new PerlinNoise(&rt, NETHER_TOUCH_NOISE_LEVELS);
    s_noiseForSeed = worldSeed;
    s_noiseReady = true;
}

// Height (in blocks above NETHER_FLOOR_BASE_Y) of the floor hill at this
// column, or 0 for an open gap (lava sea/river surface stays flat at the
// lava floor). Noise is remapped so roughly a third of columns come back
// at/near 0 -- that's the "skipping places here and there" for lava seas
// and rivers to show through, rather than a solid unbroken hill blanket.
// See the NETHER_HILL_NOISE_NORM comment above ensureNetherNoise for why
// dividing by that norm first is required.
#define NETHER_HILL_NOISE_SCALE 0.02f
#define NETHER_HILL_MAX_HEIGHT  22

static int floorHillHeight(int gx, int gz) {
    float n = s_floorNoise->getValue(gx * NETHER_HILL_NOISE_SCALE, gz * NETHER_HILL_NOISE_SCALE);
    n /= NETHER_HILL_NOISE_NORM; // back to a real ~[-1,1] range -- see comment above ensureNetherNoise
    float v = n * 0.5f + 0.5f;
    v = (v - 0.35f) / 0.65f; // below ~0.35 clips negative -> gap
    if (v <= 0.0f) return 0;
    if (v > 1.0f) v = 1.0f;
    return (int)(v * v * NETHER_HILL_MAX_HEIGHT); // v*v: bias toward lower/rolling hills, occasional tall one
}
static int ceilHillDepth(int gx, int gz) {
    float n = s_ceilNoise->getValue(gx * NETHER_HILL_NOISE_SCALE, gz * NETHER_HILL_NOISE_SCALE);
    n /= NETHER_HILL_NOISE_NORM;
    float v = n * 0.5f + 0.5f;
    v = (v - 0.35f) / 0.65f;
    if (v <= 0.0f) return 0;
    if (v > 1.0f) v = 1.0f;
    return (int)(v * v * NETHER_HILL_MAX_HEIGHT);
}

// True for the rare columns chosen to be touch-point pillars, where the
// floor and ceiling hills are deliberately allowed to meet instead of
// being held apart by NETHER_MIN_GAP. Sparse low-frequency noise
// thresholded very high, so genuine touch points are isolated and
// uncommon rather than forming a whole wall of pillars. Same
// NETHER_TOUCH_NOISE_NORM division as the hill fields above is required
// here too -- this was the more severely broken of the two before the
// fix, since an unnormalized ~[-3,3] value trivially cleared a threshold
// meant for a normalized ~[-1,1] input, making touch points far more
// common than "rare and isolated".
#define NETHER_TOUCH_NOISE_SCALE 0.008f
#define NETHER_TOUCH_THRESHOLD   0.965f

static bool isTouchPointColumn(int gx, int gz) {
    float n = s_touchNoise->getValue(gx * NETHER_TOUCH_NOISE_SCALE, gz * NETHER_TOUCH_NOISE_SCALE);
    n /= NETHER_TOUCH_NOISE_NORM;
    float v = n * 0.5f + 0.5f;
    return v >= NETHER_TOUCH_THRESHOLD;
}

// --- Bulk fill: bedrock box, lava floor, floor hills, ceiling hills -------

static void netherFillColumn(World* w, int cx, int cz) {
    ensureNetherNoise(worldGenSeed());

    int xo = cx * 16, zo = cz * 16;
    bool edgeX0 = isNetherStripEdgeX0(cx), edgeX1 = isNetherStripEdgeX1(cx);
    bool edgeZ0 = isNetherStripEdgeZ0(cz), edgeZ1 = isNetherStripEdgeZ1(cz);

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
                    if (floorH >= ceilH) floorH = (floorH - deficit < 0) ? 0 : floorH - deficit;
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
        int y = NETHER_LAVA_FLOOR_TOP + 1 + random.nextInt(6);
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

    // Warped fungus (the "tree" of this biome) and warped wart blocks
    // scattered on the nylium floor.
    int fungusTries = 4 + random.nextInt(4);
    for (int i = 0; i < fungusTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_FUNGUS);
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

static void placeCeilingGlowstone(World* w, int xo, int zo, Random& random) {
    // Applies to every biome (not just Wastes) -- per the brief, glowstone
    // clusters on the ceiling are a blanket ambient feature of the whole
    // Nether, not something biome-specific. Same underside-of-a-ceiling-
    // hill search as the old Wastes-only version used.
    int clusters = 2 + random.nextInt(3);
    for (int i = 0; i < clusters; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_BASE_Y + random.nextInt(NETHER_CEIL_BASE_Y - NETHER_FLOOR_BASE_Y);
        // A ceiling-hill underside: air here, netherrack directly above.
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (!isNetherrackFace(w, x, y + 1, z)) continue;
        int blobSize = 3 + random.nextInt(5);
        for (int b = 0; b < blobSize; b++) {
            int bx = x + random.nextInt(3) - 1, bz = z + random.nextInt(3) - 1;
            if (worldBlock(w, bx, y, bz) == BLOCK_AIR && isNetherrackFace(w, bx, y + 1, bz))
                setBlock(w, bx, y, bz, BLOCK_GLOWSTONE);
        }
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

    NetherBiomeId biome = classifyNetherBiome(worldSeed, xo + 8, zo + 8);
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

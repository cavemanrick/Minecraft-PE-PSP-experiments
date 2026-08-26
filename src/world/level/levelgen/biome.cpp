#include "world/level/levelgen/biome.h"
#include "world/level/levelgen/mcpegen_internal.h"
#include "world/level/levelgen/Random.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/world.h"
#include <math.h>

// Biome placement: each biome gets exactly one seed point, placed once per
// world (deterministic from the world seed) on a jittered grid so seeds are
// well spread out without needing rejection sampling. A column's biome is
// simply whichever seed is nearest, giving compact, non-repeating regions
// instead of the old noise-based classifier's scattered patches. Distance is
// perturbed by a low-frequency Perlin field so region borders wiggle
// naturally instead of forming razor-straight Voronoi edges.
//
// B_MUSHROOM is the one exception to "nearest seed wins": it also has a
// hard maximum radius, so it forms a small island rather than a full-size
// region, and the outer ring of that island is always carved to water. See
// classifyBiomeSpatialEx.

static const int N_BIOMES = 12;
static const BiomeId kBiomeOrder[N_BIOMES] = {
    B_TUNDRA, B_SAVANNA, B_DESERT, B_SWAMP, B_TAIGA, B_SHRUB,
    B_FOREST, B_PLAINS, B_SEASONAL, B_RAIN, B_JUNGLE, B_MUSHROOM
};

static bool  s_seedsReady = false;
static long  s_seedsForWorldSeed = 0;
static float s_seedX[N_BIOMES];
static float s_seedZ[N_BIOMES];
static int   s_mushroomIdx = -1;
static float s_islandRadius = 0.0f;
static PerlinNoise* s_borderNoise = 0;

// One noise field serves both river jobs -- the sideways meander and the
// width variation -- sampled at two different scales and two far-apart
// offsets. Perlin values at positions that distant are uncorrelated in
// practice, which is the same trick the border wobble above already uses
// with its `+ i * 37.0f` per-seed offset. Two separate PerlinNoise objects
// would have been clearer but cost another permutation table per octave
// for no behavioural gain, and this runs on a PSP.
static PerlinNoise* s_riverNoise = 0;

// Chunk extent of the OVERWORLD only, which is not the same thing as
// w->sizeX on the 1024 preset.
//
// This distinction was previously missed, with real consequences: the
// 1024 preset sets sizeX to WORLD_PRESET_1024_TOTAL_X_CHUNKS (128 chunks
// = 2048 blocks), because that total has to cover the reserved Nether/End
// strip bolted onto the right-hand side of the same flat coordinate space
// (see WORLD_NETHER_ORIGIN_CX in world.h). Spreading biome seeds across
// that total put half of them at x 1024..2047 -- inside the reserved
// strip, where mcpegen never runs. Of the eleven biomes, six (Desert,
// Swamp, Forest, Plains, Jungle, and one grid slot over) had their seed
// points in territory the player can never stand on, so those biomes
// simply never appeared in the playable overworld at all.
//
// Z is unaffected (the reserved strip does not extend the Z bound), but is
// routed through the same helper for symmetry.
// Both of these now defer to world.h rather than special-casing the 1024
// preset, because the 512 preset has reserved strips too. Keeping a local
// copy of that arithmetic here is exactly how the original bug happened.
static int overworldChunksX(const World* w) {
    if (w->sizeX == 0) return 0;                     // legacy infinite save
    return worldOverworldChunksX(w);
}
static int overworldChunksZ(const World* w) {
    return w->sizeZ;                                  // strips never extend Z
}

static void ensureBiomeSeeds(long worldSeed, const World* w) {
    if (s_seedsReady && s_seedsForWorldSeed == worldSeed) return;

    Random seedRandom(worldSeed ^ 0x610E5EEDL);

    // Infinite worlds (w->sizeX == 0) have no fixed span to spread seeds
    // across -- there's nothing for "the whole world" to mean. Fall back
    // to a large-but-bounded span (same [0, span] convention the finite
    // case already uses below, not double-sided) so biome seeds still
    // land somewhere sane near where a fresh world's spawn actually is,
    // instead of every seed collapsing to (0,0) (worldSpanX/Z would be
    // zero otherwise, and dividing the grid across a zero span put every
    // single seed at the exact same point regardless of world seed). This
    // span is arbitrary but generous -- 2048 chunks per axis, comfortably
    // larger than either pre-generated option -- since an infinite world
    // can always stream biome territory further out than this if the
    // player actually walks that far; classifyBiomeSpatial's nearest-seed
    // math doesn't require the seeds to bound where the player can go,
    // only to be spread out relative to each other.
    const int cols = 4, rows = 3; // 12 grid cells, all 12 used
    int cxChunks = overworldChunksX(w), czChunks = overworldChunksZ(w);
    float worldSpanX = (float)((cxChunks ? cxChunks : 2048) * CHUNK_SX);
    float worldSpanZ = (float)((czChunks ? czChunks : 2048) * CHUNK_SZ);
    float cellW = worldSpanX / cols;
    float cellD = worldSpanZ / rows;

    // Island radius scales with the grid cell rather than being a fixed
    // block count, so the island stays proportionate on a 512 world, a
    // 1024 world and the infinite fallback span alike. 0.28 of the smaller
    // cell dimension puts it well inside its own cell, which matters: the
    // radius has to be the binding constraint most of the time, or the
    // island degenerates back into an ordinary full-size Voronoi region on
    // whichever side has no near neighbour.
    //
    // Computed BEFORE the seed loop because the mushroom seed's placement
    // depends on it -- see the clamp below.
    float cellMin = (cellW < cellD) ? cellW : cellD;
    s_islandRadius = cellMin * 0.28f;

    s_mushroomIdx = -1;
    for (int i = 0; i < N_BIOMES; i++) {
        int gx = i % cols, gz = i / cols;
        float centerX = gx * cellW + cellW * 0.5f;
        float centerZ = gz * cellD + cellD * 0.5f;
        // Jitter within the cell, keeping a margin so seeds don't land right
        // on a cell edge (which would crowd two regions' borders together).
        float jitterX = (seedRandom.nextFloat() - 0.5f) * cellW * 0.5f;
        float jitterZ = (seedRandom.nextFloat() - 0.5f) * cellD * 0.5f;
        s_seedX[i] = centerX + jitterX;
        s_seedZ[i] = centerZ + jitterZ;

        if (kBiomeOrder[i] == B_MUSHROOM) {
            s_mushroomIdx = i;
            // Keep the whole island inside the world. Its grid cell is the
            // bottom-right one, so on a finite world the jitter can push
            // the seed close enough to the edge that the island's outer
            // rim -- moat included -- would run past the boundary and be
            // cut off by Level::getCubes' invisible wall instead of by
            // water. That would break the one hard guarantee this biome
            // has: fully surrounded, every time.
            //
            // Only for finite worlds. An infinite world has no edge to
            // collide with, and clamping against the arbitrary 2048-chunk
            // fallback span would be meaningless there.
            if (cxChunks && czChunks) {
                float lo = s_islandRadius + 1.0f;
                float hiX = worldSpanX - s_islandRadius - 1.0f;
                float hiZ = worldSpanZ - s_islandRadius - 1.0f;
                if (s_seedX[i] < lo)  s_seedX[i] = lo;
                if (s_seedX[i] > hiX) s_seedX[i] = hiX;
                if (s_seedZ[i] < lo)  s_seedZ[i] = lo;
                if (s_seedZ[i] > hiZ) s_seedZ[i] = hiZ;
            }
        }
    }

    delete s_borderNoise;
    s_borderNoise = new PerlinNoise(&seedRandom, 2);

    // Drawn from the same seedRandom stream, after the border noise, so it
    // is deterministic per world seed like everything else here. Anything
    // added to this function in future must go AFTER this line or every
    // existing world's rivers move.
    delete s_riverNoise;
    s_riverNoise = new PerlinNoise(&seedRandom, 2);

    s_seedsForWorldSeed = worldSeed;
    s_seedsReady = true;
}

// Does the seam between these two biomes carry a river? Hashed from the
// unordered pair plus the world seed, so the answer is the same whichever
// side of the seam the column being classified sits on -- which matters,
// because a column just west of the border finds (A,B) and its neighbour
// just east finds (B,A), and a river that existed on only one bank would
// be a waterfall down a cliff instead of a river.
static bool pairHasRiver(long worldSeed, int a, int b) {
    int lo = (a < b) ? a : b;
    int hi = (a < b) ? b : a;
    unsigned int h = (unsigned int)worldSeed;
    h ^= (unsigned int)lo * 0x9E3779B9u;
    h ^= (unsigned int)hi * 0x85EBCA6Bu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (h % 100u) < RIVER_PAIR_PERCENT;
}

static float riverSmoothstep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

float mushroomLandLift(float margin) {
    float inland = margin - MUSHROOM_MOAT_WIDTH;
    if (inland <= 0.0f) return 0.0f;
    if (inland >= MUSHROOM_SHORE_FADE) return 1.0f;
    float t = inland / MUSHROOM_SHORE_FADE;
    return t * t * (3.0f - 2.0f * t); // smoothstep: flat at both ends, no crease
}

BiomeId classifyBiomeSpatialEx(long worldSeed, const World* w, int worldX, int worldZ,
                               float* mushroomMargin,
                               float* riverChannel, float* riverValley) {
    ensureBiomeSeeds(worldSeed, w);

    float bx = (float)worldX, bz = (float)worldZ;
    int best = 0, second = -1;
    float bestD2 = 1e18f, secondD2 = 1e18f;
    for (int i = 0; i < N_BIOMES; i++) {
        float dx = bx - s_seedX[i], dz = bz - s_seedZ[i];
        float d2 = dx * dx + dz * dz;

        // Perturb each seed's effective distance a little so the boundary
        // between two regions wiggles instead of being a straight line.
        float wobble = s_borderNoise->getValue(worldX * 0.01f, worldZ * 0.01f + i * 37.0f);
        d2 += wobble * 900.0f;

        if (d2 < bestD2)      { secondD2 = bestD2; second = best; bestD2 = d2; best = i; }
        else if (d2 < secondD2) { secondD2 = d2; second = i; }
    }

    if (mushroomMargin) *mushroomMargin = 0.0f;
    if (riverChannel)   *riverChannel   = 0.0f;
    if (riverValley)    *riverValley    = 0.0f;

    // --- Rivers ----------------------------------------------------------
    // Computed here, inside the one function that has already paid for the
    // nearest-seed loop, and before the mushroom branch below returns.
    if ((riverChannel || riverValley) && second >= 0 &&
        best != s_mushroomIdx && second != s_mushroomIdx &&
        pairHasRiver(worldSeed, best, second)) {

        // Distance to the seam. For two seed points, half the difference of
        // the two distances is the perpendicular distance to their bisector
        // -- exact on the line joining them and close enough everywhere
        // else. Same clamp-before-sqrt guard the mushroom code below uses,
        // for the same reason: the wobble term is signed.
        float dNear = sqrtf(bestD2   > 0.0f ? bestD2   : 0.0f);
        float dFar  = sqrtf(secondD2 > 0.0f ? secondD2 : 0.0f);
        float edge  = (dFar - dNear) * 0.5f;

        // Signed, so the field is continuous across the seam instead of
        // folding at it. The sign is taken from the seed INDICES rather
        // than from the geometry, because that is the one thing that
        // reliably flips when you step across the border: a column on the
        // far side finds the same two seeds with best and second swapped.
        // Without this, adding the meander below would widen the river on
        // one bank and narrow it on the other rather than moving it.
        float signedEdge = (best < second) ? edge : -edge;

        // Wander the channel sideways off the true bisector. Low frequency
        // (0.015 -> features about 65 blocks long) so it reads as a river
        // bending through the landscape, not as jitter.
        signedEdge += s_riverNoise->getValue(worldX * 0.015f, worldZ * 0.015f) * RIVER_MEANDER_AMP;

        float dist = (signedEdge < 0.0f) ? -signedEdge : signedEdge;

        if (riverChannel) {
            // Width varies along the run so the river narrows and widens
            // the way a real one does. Offset far from the meander sample
            // so the two fields don't move together -- a river that got
            // wider every time it bent the same way would look mechanical.
            float wn = s_riverNoise->getValue(worldX * 0.03f + 811.0f,
                                              worldZ * 0.03f + 517.0f);
            float halfW = RIVER_HALF_WIDTH * (1.0f + 0.35f * wn);
            if (halfW < 1.5f) halfW = 1.5f;
            if (dist < halfW) *riverChannel = 1.0f - dist / halfW;
        }

        if (riverValley) {
            // Smoothstepped rather than linear: a linear valley has a
            // visible crease where it meets undisturbed terrain, which is
            // exactly the artefact the mushroom shore fade exists to avoid.
            if (dist < RIVER_VALLEY_HALF_WIDTH)
                *riverValley = riverSmoothstep(1.0f - dist / RIVER_VALLEY_HALF_WIDTH);
        }
    }

    if (best != s_mushroomIdx) return kBiomeOrder[best];

    // The wobble term is signed and can push d2 slightly negative near a
    // seed point, so clamp before the square root rather than handing sqrtf
    // a negative and getting a NaN that would silently poison every
    // comparison below.
    float dm = sqrtf(bestD2 > 0.0f ? bestD2 : 0.0f);
    float dOther = sqrtf(secondD2 > 0.0f ? secondD2 : 0.0f);

    // Hard radius: beyond it the column is NOT mushroom, and is handed to
    // whichever biome was runner-up. Without this the mushroom biome would
    // claim its entire Voronoi cell and only the inner disc would actually
    // be island, leaving a wide belt of "mushroom" territory that looked
    // like nothing in particular.
    if (dm >= s_islandRadius)
        return (second >= 0) ? kBiomeOrder[second] : kBiomeOrder[best];

    // How far inside the island's outer edge this column sits. The edge is
    // whichever constraint binds first: the hard radius, or the border with
    // the nearest other biome. Taking the minimum is what guarantees the
    // moat is a complete ring -- if only the radius were used, a stretch of
    // shoreline that happened to be cut off by a neighbouring region would
    // have no water on it.
    float marginRadius = s_islandRadius - dm;
    float marginNeighbour = dOther - dm;
    float margin = (marginRadius < marginNeighbour) ? marginRadius : marginNeighbour;
    if (mushroomMargin) *mushroomMargin = margin;
    return B_MUSHROOM;
}

BiomeId classifyBiomeSpatial(long worldSeed, const World* w, int worldX, int worldZ) {
    return classifyBiomeSpatialEx(worldSeed, w, worldX, worldZ, 0);
}

void biomeSurface(BiomeId b, unsigned char* top, unsigned char* material) {
    if (b == B_DESERT)        { *top = BLOCK_SAND; *material = BLOCK_SAND; }
    // Mycelium over ordinary dirt, matching vanilla mushroom fields. Moat
    // columns are classified B_MUSHROOM too, but they never reach this
    // top-of-column case: buildSurfacesChunk carves them below sea level
    // first, so their exposed block is taken from *material (dirt), not
    // *top. See the moat carve there.
    else if (b == B_MUSHROOM) { *top = BLOCK_MYCELIUM; *material = BLOCK_DIRT; }
    else                      { *top = BLOCK_GRASS; *material = BLOCK_DIRT; }
}

void McpeGen::computeBiome(int chunkX, int chunkZ) {
    int x = chunkX * 16, z = chunkZ * 16;
    rawTemp     = temperatureMap.getRegion(rawTemp,     x, z, 16, 16, BIOME_TEMP_SCALE,  BIOME_TEMP_SCALE,  0.25f);
    rawDownfall = downfallMap.getRegion(rawDownfall,    x, z, 16, 16, BIOME_DOWN_SCALE,  BIOME_DOWN_SCALE,  0.3333f);
    rawNoise    = noiseMap.getRegion(rawNoise,          x, z, 16, 16, BIOME_NOISE_SCALE, BIOME_NOISE_SCALE, 0.588f);

    for (int pp = 0; pp < 16 * 16; pp++) {
        float noise = (rawNoise[pp] * 1.1f + 0.5f);

        float split2 = 0.01f, split1 = 1 - split2;
        float temperature = (rawTemp[pp] * 0.15f + 0.7f) * split1 + noise * split2;
        split2 = 0.002f; split1 = 1 - split2;
        float downfall = (rawDownfall[pp] * 0.15f + 0.5f) * split1 + noise * split2;

        temperature = 1 - ((1 - temperature) * (1 - temperature));
        if (temperature < 0) temperature = 0;
        if (downfall < 0) downfall = 0;
        if (temperature > 1) temperature = 1;
        if (downfall > 1) downfall = 1;

        mTemp[pp] = temperature;
        mDownfall[pp] = downfall;
    }
}

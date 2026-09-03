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

// 13 biomes placed on a 4x4 (16-cell) jittered grid -- three cells are
// left empty each world (see the shuffle in ensureBiomeSeeds). Grown from
// the old 4x3/12 exact fit to make room for B_DARK_FOREST. The spare
// cells are deliberately kept rather than shrinking back to an exact
// 13-cell rectangle, so a future biome addition again only has to claim
// one of the already-empty slots instead of re-gridding to a new exact
// fit.
static const int N_BIOMES = 13;
static const int N_GRID_CELLS = 16;
static const BiomeId kBiomeOrder[N_BIOMES] = {
    B_TUNDRA, B_SAVANNA, B_DESERT, B_SWAMP, B_TAIGA, B_SHRUB,
    B_FOREST, B_PLAINS, B_SEASONAL, B_RAIN, B_JUNGLE, B_MUSHROOM,
    B_DARK_FOREST
};

static bool  s_seedsReady = false;
static long  s_seedsForWorldSeed = 0;
static float s_seedX[N_BIOMES];
static float s_seedZ[N_BIOMES];
static int   s_gridCellOfSeed[N_BIOMES]; // which of the N_GRID_CELLS cells each seed landed in
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
    const int cols = 4, rows = 4; // N_GRID_CELLS (16) cells; 13 used, 3 left empty
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
    // depends on it -- see the ocean search below.
    float cellMin = (cellW < cellD) ? cellW : cellD;
    s_islandRadius = cellMin * 0.28f;

    // Which of the N_GRID_CELLS cells get a biome: shuffle 0..15 with the
    // same per-world seedRandom stream, then take the first N_BIOMES. This
    // spreads the three empty cells around the grid (rather than always
    // leaving the last row bare, which is what simply looping i<N_BIOMES
    // over sequential cells would do) and keeps a biome's cell -- and so
    // its rough map position -- stable as more biomes are added later,
    // since existing biomes keep drawing from the same early positions in
    // the shuffle only so long as the shuffle algorithm and biome count
    // don't change together. Adding a 14th biome will still reshuffle
    // everyone; that tradeoff already exists for any grid resize.
    int cellOrder[N_GRID_CELLS];
    for (int c = 0; c < N_GRID_CELLS; c++) cellOrder[c] = c;
    for (int c = N_GRID_CELLS - 1; c > 0; c--) {
        int j = seedRandom.nextInt(c + 1);
        int tmp = cellOrder[c]; cellOrder[c] = cellOrder[j]; cellOrder[j] = tmp;
    }

    s_mushroomIdx = -1;
    for (int i = 0; i < N_BIOMES; i++) {
        int cell = cellOrder[i];
        s_gridCellOfSeed[i] = cell;
        int gx = cell % cols, gz = cell / cols;
        float centerX = gx * cellW + cellW * 0.5f;
        float centerZ = gz * cellD + cellD * 0.5f;
        // Jitter within the cell, keeping a margin so seeds don't land right
        // on a cell edge (which would crowd two regions' borders together).
        float jitterX = (seedRandom.nextFloat() - 0.5f) * cellW * 0.5f;
        float jitterZ = (seedRandom.nextFloat() - 0.5f) * cellD * 0.5f;
        s_seedX[i] = centerX + jitterX;
        s_seedZ[i] = centerZ + jitterZ;

        if (kBiomeOrder[i] == B_MUSHROOM) s_mushroomIdx = i;
    }

    // The mushroom island no longer carves its own moat -- it has to sit
    // in real, already-existing ocean instead, wide enough that the whole
    // island (moat-free now, so its true edge is s_islandRadius itself,
    // not radius-minus-moat) is over deep water on every side. That means
    // searching for a genuine site rather than trusting the grid-cell
    // jitter above, which only ever produced a position, never a
    // guarantee about what terrain would generate there.
    //
    // A throwaway McpeGen, not mcpegen.cpp's file-static g_gen: this runs
    // the first time ANY code asks classifyBiomeSpatialEx a question,
    // which can happen before g_gen exists for this world (or for a world
    // that never becomes the active one, e.g. a menu preview), and
    // g_gen's lifetime is deliberately opaque to biome.cpp. McpeGen's
    // whole state is a deterministic function of worldSeed alone, so a
    // second instance built from the same seed reproduces byte-identical
    // noise fields to whatever g_gen (if any) exists elsewhere -- this
    // is NOT a second, different terrain generator, just the one formula
    // evaluated standalone. The construction cost (permutation tables,
    // same as g_gen already pays once per world) is paid once here, not
    // per chunk or per search attempt.
    if (s_mushroomIdx >= 0) {
        McpeGen probe(worldSeed);

        // Ring of sample points at the island's true outer edge (no moat
        // margin to lean on any more), plus the centre -- a shallow shelf
        // near a shore could pass a centre-only check while leaving part
        // of the island's rim on dry land, so the perimeter has to be
        // checked too, not just assumed from one interior sample.
        const int PERIMETER_SAMPLES = 8;
        const int OCEAN_SEARCH_ATTEMPTS = 96;

        bool found = false;
        float foundX = 0.0f, foundZ = 0.0f;
        for (int attempt = 0; attempt < OCEAN_SEARCH_ATTEMPTS && !found; attempt++) {
            // Reuse the seed's own grid cell as the search area rather
            // than scanning the whole world: the mushroom biome is still
            // meant to occupy roughly that region relative to the other
            // biomes, just at whichever exact spot within it turns out to
            // be real ocean rather than at a fixed jittered point. Cell
            // index comes from s_gridCellOfSeed, not s_mushroomIdx itself
            // -- the shuffle above means those are no longer the same
            // number.
            int cell = s_gridCellOfSeed[s_mushroomIdx];
            int gx = cell % cols, gz = cell / cols;
            float lo = s_islandRadius + 2.0f; // +2: a little slack past the
                                               // exact edge so a barely-
                                               // passing perimeter sample
                                               // isn't immediately retested
                                               // right on the search area's
                                               // own boundary
            float cx = gx * cellW + lo + seedRandom.nextFloat() * (cellW - 2.0f * lo);
            float cz = gz * cellD + lo + seedRandom.nextFloat() * (cellD - 2.0f * lo);
            if (cxChunks && czChunks) {
                // Same finite-world edge guard the old fixed placement
                // used, now applied to each candidate rather than to one
                // fixed point.
                float hiX = worldSpanX - s_islandRadius - 1.0f;
                float hiZ = worldSpanZ - s_islandRadius - 1.0f;
                float loBound = s_islandRadius + 1.0f;
                if (cx < loBound || cx > hiX || cz < loBound || cz > hiZ) continue;
            }

            bool allOcean = McpeGen_isOceanAt(&probe, cx, cz);
            for (int p = 0; allOcean && p < PERIMETER_SAMPLES; p++) {
                float ang = (float)p / (float)PERIMETER_SAMPLES * 6.2831853f;
                float px = cx + cosf(ang) * s_islandRadius;
                float pz = cz + sinf(ang) * s_islandRadius;
                if (!McpeGen_isOceanAt(&probe, px, pz)) allOcean = false;
            }

            if (allOcean) { found = true; foundX = cx; foundZ = cz; }
        }

        if (found) {
            s_seedX[s_mushroomIdx] = foundX;
            s_seedZ[s_mushroomIdx] = foundZ;
        } else {
            // No ocean found anywhere in the search budget: don't build
            // the biome at all rather than fall back to the old fixed
            // placement, which had no relationship to real terrain.
            //
            // Banish the seed itself rather than merely setting
            // s_mushroomIdx to -1: classifyBiomeSpatialEx's nearest-seed
            // loop below runs over ALL N_BIOMES positions regardless of
            // s_mushroomIdx, and its "best != s_mushroomIdx" check only
            // ever guarded the RETURN VALUE, not whether this slot could
            // still win nearest-seed in the first place -- kBiomeOrder[i]
            // is a fixed array, so kBiomeOrder[s_mushroomIdx] is B_MUSHROOM
            // no matter what s_mushroomIdx's own value is. Setting
            // s_mushroomIdx to -1 alone would leave the seed sitting at
            // its ordinary grid-cell position, geometrically eligible to
            // still be the nearest seed for every column around it, and
            // classifyBiomeSpatialEx would return B_MUSHROOM for them via
            // kBiomeOrder[best] regardless -- the exact bug this comment
            // exists to avoid reintroducing. Moving the position far
            // outside any real world span means the distance-squared for
            // this seed is always astronomically larger than every other
            // seed's, so it can never be picked as best or second.
            s_seedX[s_mushroomIdx] = -1.0e9f;
            s_seedZ[s_mushroomIdx] = -1.0e9f;
            s_mushroomIdx = -1;
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

#include "world/level/levelgen/mcpegen.h"
#include "world/level/chunk/chunk_cache.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/caves.h"
#include "world/level/levelgen/gen_features.h"
#include "world/level/levelgen/village_gen.h"
#include "world/level/levelgen/dungeon_gen.h"
#include "world/level/levelgen/PerlinNoise.h"
#include "world/level/world.h"

#include <stdlib.h>
#include <pspkernel.h>
#include <pspthreadman.h>

#include "world/level/levelgen/mcpegen_internal.h"
#include "world/level/tile/nether_portal.h"
#include "world/level/levelgen/biome.h"

#define MCPE_DEPTH    128

#define NCELL_W       4
#define NCELL_H       8

// How far below sea level the density surface must sit for a probed point
// to count as real open ocean, not a shallow puddle or a beach the
// island's own radius would immediately swallow back into land. 6 blocks
// is comfortably past the sand/beach transition band real coastline
// generates, without demanding the deepest part of an ocean trench.
#define MUSHROOM_OCEAN_MIN_DEPTH 6

// Single-point reproduction of getHeights' density formula, collapsed to
// one (x,z) column instead of a region: island siting needs "is there
// real ocean at this exact spot", not the batched per-chunk sampling
// getHeights does for actual chunk generation, and PerlinNoise::getValue
// (a genuine single-point query, unlike getRegion's batch path) makes
// that cheap enough to call at a handful of candidate sites without
// materializing a full region buffer for each one.
//
// Deliberately reimplements getHeights' math rather than calling it: this
// runs BEFORE the mushroom seed's own position is decided, so it must
// not itself depend on classifyBiomeSpatialEx (which needs s_mushroomIdx
// to already be resolved) the way getHeights' own island-lift step does.
// A probe site is never going to land inside the mushroom island itself
// (that's the point), so the island-lift term getHeights adds is
// correctly absent here.
static float McpeGen_densityAt(McpeGen* g, float wx, float wz, float wy) {
    // Every 2D-looking noise query in getHeights/computeBiome actually
    // goes through PerlinNoise::getRegion's 2-arg-scale overload, which
    // forwards to the 3D getRegion with y FIXED AT 10.0f (see
    // PerlinNoise.cpp: "return getRegion(sr, x, 10.0f, z, ...)"). The
    // getValue(x,y) 2-argument overload is a genuinely different, real 2D
    // query -- it does NOT reproduce that fixed-y-10 behaviour. Every one
    // of these calls therefore has to go through getValue(x,y,z) with
    // y=10.0f explicitly, not the shorter 2-argument overload, or the
    // sampled value is for a different noise field entirely.
    const float FIXED_Y = 10.0f;

    float temperature = g->temperatureMap.getValue(wx * BIOME_TEMP_SCALE, FIXED_Y, wz * BIOME_TEMP_SCALE) * 0.15f + 0.7f;
    float noise = g->noiseMap.getValue(wx * BIOME_NOISE_SCALE, FIXED_Y, wz * BIOME_NOISE_SCALE) * 1.1f + 0.5f;
    temperature = temperature * (1.0f - 0.01f) + noise * 0.01f;
    temperature = 1.0f - ((1.0f - temperature) * (1.0f - temperature));
    if (temperature < 0.0f) temperature = 0.0f;
    if (temperature > 1.0f) temperature = 1.0f;

    float downfall = g->downfallMap.getValue(wx * BIOME_DOWN_SCALE, FIXED_Y, wz * BIOME_DOWN_SCALE) * 0.15f + 0.5f;
    downfall = downfall * (1.0f - 0.002f) + noise * 0.002f;
    if (downfall < 0.0f) downfall = 0.0f;
    if (downfall > 1.0f) downfall = 1.0f;
    downfall *= temperature; // matches getHeights' mDownfall[pp] * temperature

    float dd = 1.0f - downfall;
    dd = dd * dd; dd = dd * dd;
    dd = 1.0f - dd;

    float s = 684.412f, hs = 684.412f;
    float scale = ((g->scaleNoise.getValue(wx * 1.121f, FIXED_Y, wz * 1.121f) + 256.0f) / 512.0f);
    scale *= dd;
    if (scale > 1.0f) scale = 1.0f;
    if (scale < 0.0f) scale = 0.0f;
    scale += 0.5f;

    float depth = (g->depthNoise.getValue(wx * 200.0f, FIXED_Y, wz * 200.0f) / 8000.0f);
    if (depth < 0.0f) depth = -depth * 0.3f;
    depth = depth * 3.0f - 2.0f;
    int ySize = (WORLD_H / NCELL_H) + 1;
    if (depth < 0.0f) {
        depth = depth / 2.0f;
        if (depth < -1.0f) depth = -1.0f;
        depth = depth / 1.4f;
        depth /= 2.0f;
        scale = 0.0f;
    } else {
        if (depth > 1.0f) depth = 1.0f;
        depth = depth / 8.0f;
    }
    depth = depth * ySize / 16.0f;

    float yCenter = ySize / 2.0f + depth * 4.0f;

    float yOffs = (wy - yCenter) * 12.0f / scale;
    if (yOffs < 0.0f) yOffs *= 4.0f;

    // These three ARE genuinely 3D queries in getHeights (perlinNoise1/
    // lperlinNoise1/lperlinNoise2's own getRegion calls pass real x,y,z
    // sizes, not the 2D overload), so wy is a real coordinate here, not a
    // fixed dummy -- no FIXED_Y substitution for these three.
    float bb = g->lperlinNoise1.getValue(wx * s, wy * hs, wz * s) / 512.0f;
    float cc = g->lperlinNoise2.getValue(wx * s, wy * hs, wz * s) / 512.0f;
    float v  = (g->perlinNoise1.getValue(wx * (s / 80.0f), wy * (hs / 160.0f), wz * (s / 80.0f)) / 10.0f + 1.0f) / 2.0f;

    float val;
    if (v < 0.0f) val = bb;
    else if (v > 1.0f) val = cc;
    else val = bb + (cc - bb) * v;
    val -= yOffs;
    return val;
}

// True if real open ocean exists at (wx,wz): the density surface (density
// crosses from solid to air, scanning down from just above sea level)
// sits at least MUSHROOM_OCEAN_MIN_DEPTH blocks under sea level. yCenter
// in the density field is expressed in NCELL_H (8-block) units, so the
// scan below steps by whole NCELL_H blocks -- fine resolution isn't
// needed for a yes/no ocean check, only for actually building terrain.
bool McpeGen_isOceanAt(McpeGen* g, float wx, float wz) {
    const int waterHeight = 64;
    int surfaceY = -1;
    for (int y = waterHeight + NCELL_H; y >= 0; y -= NCELL_H) {
        float yUnits = (float)y / (float)NCELL_H;
        if (McpeGen_densityAt(g, wx / NCELL_W, wz / NCELL_W, yUnits) > 0.0f) {
            surfaceY = y;
            break;
        }
    }
    if (surfaceY < 0) return false; // no solid ground found at all in range -- treat as not-ocean rather than guess
    return (waterHeight - surfaceY) >= MUSHROOM_OCEAN_MIN_DEPTH;
}


McpeGen::McpeGen(long seed)
  : random(seed),

    rndTemp((long)((unsigned int)seed * 9871u)),
    rndDownfall((long)((unsigned int)seed * 39811u)),
    rndNoise((long)((unsigned int)seed * 543321u)),
    lperlinNoise1(&random, 16), lperlinNoise2(&random, 16), perlinNoise1(&random, 8),
    perlinNoise2(&random, 4), perlinNoise3(&random, 4),
    scaleNoise(&random, 10), depthNoise(&random, 16), forestNoise(&random, 8),
    temperatureMap(&rndTemp, 4), downfallMap(&rndDownfall, 4), noiseMap(&rndNoise, 2),
    buffer(0), pnr(0), ar(0), br(0), sr(0), dr(0),
    rawTemp(0), rawDownfall(0), rawNoise(0), worldSeed(seed)
{
    buffer = new float[1024];
}
McpeGen::~McpeGen() {
    delete[] buffer; delete[] pnr; delete[] ar; delete[] br; delete[] sr; delete[] dr;
    delete[] rawTemp; delete[] rawDownfall; delete[] rawNoise;
}

// How much to raise the mushroom island, in getHeights' own yCenter units.
// One unit here is NCELL_H (8) blocks, so 0.75 is about 6 blocks -- enough
// to clear sea level (y=64, which is yCenter 8) reliably without turning the
// island into a plateau. Applied AFTER the normal depth term rather than
// replacing it, so the island keeps whatever rolling shape the noise gave it.
#define MUSHROOM_ISLAND_LIFT 0.75f

// How far the land is pulled down along a river seam, in the same yCenter
// units as MUSHROOM_ISLAND_LIFT above -- one unit is NCELL_H (8) blocks, so
// 0.7 is a little under six blocks at the centre of the valley, tapering
// smoothly to nothing at RIVER_VALLEY_HALF_WIDTH.
//
// This is the half of rivers that makes them look like rivers. The channel
// carve in buildSurfacesChunk can only ever REMOVE material, so on its own
// it would cut a slot with vertical sides wherever the seam happened to
// cross high ground. Sinking a wide valley here first means the water
// almost always ends up at the bottom of ground that already slopes toward
// it, and the carve is left doing very little work.
//
// Kept well under the island lift because it applies along every river
// seam in the world rather than to one island: overdo it and the map turns
// into a set of plateaus separated by canyons.
#define RIVER_VALLEY_DROP 0.7f

// Above this height the channel carve gives up rather than cutting a gorge
// through a hillside. With the valley drop above this is rare -- it is a
// backstop for the case where a seam crosses genuinely mountainous terrain,
// where a river simply stopping reads better than a vertical-walled trench
// full of water. Sea level is 64, so this allows a river to cut about ten
// blocks into rising ground before it gives up.
#define RIVER_CUT_CEILING_Y 74

// Bed of the channel at its deepest (centre) and shallowest (bank). Water
// fills from bed+1 up to sea level - 1 (y=63), so the centre of a river is
// three blocks deep and the edges are one. Both values sit inside the
// waterHeight-4..waterHeight+1 band that the surface pass treats as beach,
// so the whole bed gets dressed in sand or gravel rather than dirt.
#define RIVER_BED_CENTRE_Y 60
#define RIVER_BED_BANK_Y   62

float* McpeGen::getHeights(const World* w, int x, int y, int z, int xSize, int ySize, int zSize) {
    float s = 1 * 684.412f;
    float hs = 1 * 684.412f;

    sr = scaleNoise.getRegion(sr, x, z, xSize, zSize, 1.121f, 1.121f, 0.5f);
    dr = depthNoise.getRegion(dr, x, z, xSize, zSize, 200.0f, 200.0f, 0.5f);

    pnr = perlinNoise1.getRegion(pnr, (float)x, (float)y, (float)z, xSize, ySize, zSize, s / 80.0f, hs / 160.0f, s / 80.0f);
    ar = lperlinNoise1.getRegion(ar, (float)x, (float)y, (float)z, xSize, ySize, zSize, s, hs, s);
    br = lperlinNoise2.getRegion(br, (float)x, (float)y, (float)z, xSize, ySize, zSize, s, hs, s);

    int p = 0;
    int pp = 0;

    int wScale = 16 / xSize;
    for (int xx = 0; xx < xSize; xx++) {
        int xp = xx * wScale + wScale / 2;
        for (int zz = 0; zz < zSize; zz++) {
            int zp = zz * wScale + wScale / 2;
            float temperature = mTemp[xp * 16 + zp];
            float downfall = mDownfall[xp * 16 + zp] * temperature;
            float dd = 1 - downfall;
            dd = dd * dd;
            dd = dd * dd;
            dd = 1 - dd;

            float scale = ((sr[pp] + 256.0f) / 512);
            scale *= dd;
            if (scale > 1) scale = 1;

            float depth = (dr[pp] / 8000.0f);
            if (depth < 0) depth = -depth * 0.3f;
            depth = depth * 3.0f - 2.0f;

            if (depth < 0) {
                depth = depth / 2;
                if (depth < -1) depth = -1;
                depth = depth / 1.4f;
                depth /= 2;
                scale = 0;
            } else {
                if (depth > 1) depth = 1;
                depth = depth / 8;
            }

            if (scale < 0) scale = 0;
            scale = (scale) + 0.5f;
            depth = depth * ySize / 16;

            float yCenter = ySize / 2.0f + depth * 4;

            // Mushroom island: raise the land so it is reliably ABOVE sea
            // level. Without this the island would inherit whatever the
            // density field happened to produce, and any island that landed
            // on ocean terrain would be a moat around nothing.
            //
            // (x + xx) and (z + zz) are in NCELL_W units, not blocks -- see
            // prepareChunk's call, which passes chunkX * (16 / NCELL_W).
            // Multiplying back up by NCELL_W is what makes this sample the
            // same world position the column loop will later write to.
            //
            // This is 25 classifications per chunk (a 5x5 grid), not 256:
            // cheap enough to sit in the density path, which is why the lift
            // lives here rather than being faked afterwards by stacking
            // blocks on top of a finished column.
            //
            // The river valley rides along on the same classification call
            // -- the nearest-seed loop is the expensive part and it has
            // already run, so asking it for the valley field as well costs
            // essentially nothing. The two never overlap: seams touching
            // the mushroom island are excluded from rivers entirely (see
            // pairHasRiver in biome.cpp), so the lift and the drop can
            // never both be non-zero for one sample.
            {
                float margin = 0.0f, valley = 0.0f;
                BiomeId gb = classifyBiomeSpatialEx(worldSeed, w,
                                                    (x + xx) * NCELL_W, (z + zz) * NCELL_W,
                                                    &margin, 0, &valley);
                if (gb == B_MUSHROOM)
                    yCenter += MUSHROOM_ISLAND_LIFT * mushroomLandLift(margin);
                if (valley > 0.0f)
                    yCenter -= RIVER_VALLEY_DROP * valley;
            }

            pp++;

            for (int yy = 0; yy < ySize; yy++) {
                float val = 0;

                float yOffs = (yy - (yCenter)) * 12 / scale;
                if (yOffs < 0) yOffs *= 4;

                float bb = ar[p] / 512;
                float cc = br[p] / 512;

                float v = (pnr[p] / 10 + 1) / 2;
                if (v < 0) val = bb;
                else if (v > 1) val = cc;
                else val = bb + (cc - bb) * v;
                val -= yOffs;

                if (yy > ySize - 4) {
                    float slide = (yy - (ySize - 4)) / (4 - 1.0f);
                    val = val * (1 - slide) + -10 * slide;
                }

                buffer[p] = val;
                p++;
            }
        }
    }
    return buffer;
}

void McpeGen::prepareChunk(World* w, int chunkX, int chunkZ) {
    int xChunks = 16 / NCELL_W;
    int xSize = xChunks + 1;
    int ySize = 128 / NCELL_H + 1;
    int zSize = xChunks + 1;

    getHeights(w, chunkX * xChunks, 0, chunkZ * xChunks, xSize, ySize, zSize);

    for (int xc = 0; xc < xChunks; xc++) {
        for (int zc = 0; zc < xChunks; zc++) {
            for (int yc = 0; yc < 128 / NCELL_H; yc++) {
                float yStep = 1 / (float) NCELL_H;
                float s0 = buffer[((xc + 0) * zSize + (zc + 0)) * ySize + (yc + 0)];
                float s1 = buffer[((xc + 0) * zSize + (zc + 1)) * ySize + (yc + 0)];
                float s2 = buffer[((xc + 1) * zSize + (zc + 0)) * ySize + (yc + 0)];
                float s3 = buffer[((xc + 1) * zSize + (zc + 1)) * ySize + (yc + 0)];

                float s0a = (buffer[((xc + 0) * zSize + (zc + 0)) * ySize + (yc + 1)] - s0) * yStep;
                float s1a = (buffer[((xc + 0) * zSize + (zc + 1)) * ySize + (yc + 1)] - s1) * yStep;
                float s2a = (buffer[((xc + 1) * zSize + (zc + 0)) * ySize + (yc + 1)] - s2) * yStep;
                float s3a = (buffer[((xc + 1) * zSize + (zc + 1)) * ySize + (yc + 1)] - s3) * yStep;

                for (int y = 0; y < NCELL_H; y++) {
                    float xStep = 1 / (float) NCELL_W;
                    float _s0 = s0;
                    float _s1 = s1;
                    float _s0a = (s2 - s0) * xStep;
                    float _s1a = (s3 - s1) * xStep;

                    for (int x = 0; x < NCELL_W; x++) {
                        float zStep = 1 / (float) NCELL_W;
                        float val = _s0;
                        float vala = (_s1 - _s0) * zStep;

                        for (int z = 0; z < NCELL_W; z++) {
                            int lx = xc * NCELL_W + x;
                            int lz = zc * NCELL_W + z;
                            int gx = chunkX * 16 + lx;
                            int gz = chunkZ * 16 + lz;
                            int gy = yc * NCELL_H + y;

                            float temp = mTemp[lx * 16 + lz];
                            unsigned char id;
                            if (val > 0)        id = BLOCK_STONE;
                            else if (gy < 64)   id = (temp < 0.5f && gy >= 63) ? BLOCK_ICE : BLOCK_CALM_WATER;
                            else                id = BLOCK_AIR;

                            blockPut(w, gx, gy, gz, id);

                            val += vala;
                        }
                        _s0 += _s0a;
                        _s1 += _s1a;
                    }

                    s0 += s0a; s1 += s1a; s2 += s2a; s3 += s3a;
                }
            }
        }
    }
}

void McpeGen::buildSurfacesChunk(World* w, int chunkX, int chunkZ) {
    const int waterHeight = 64;
    int xOffs = chunkX, zOffs = chunkZ;
    random.setSeed((long)(xOffs * 341872712l + zOffs * 132899541l));
    float s = 1 / 32.0f;
    perlinNoise2.getRegion(sandBuffer,   (float)(xOffs * 16), (float)(zOffs * 16), 0, 16, 16, 1, s,     s,     1);
    perlinNoise2.getRegion(gravelBuffer, (float)(xOffs * 16), 109.01340f, (float)(zOffs * 16), 16, 1, 16, s,     1,     s);
    perlinNoise3.getRegion(depthBuffer,  (float)(xOffs * 16), (float)(zOffs * 16), 0, 16, 16, 1, s * 2, s * 2, s * 2);

    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            float temp = 1;

            float mushMargin = 0.0f, riverT = 0.0f;
            BiomeId biome = classifyBiomeSpatialEx(worldSeed, w, xOffs * 16 + x, zOffs * 16 + z,
                                                   &mushMargin, &riverT);
            unsigned char bTop, bMat;
            biomeSurface(biome, &bTop, &bMat);

            bool sand   = (sandBuffer[z + x * 16]   + random.nextFloat() * 0.2f) > 0;
            bool gravel = (gravelBuffer[z + x * 16] + random.nextFloat() * 0.2f) > 3;
            int  runDepth = (int)(depthBuffer[z + x * 16] / 3 + 3 + random.nextFloat() * 0.25f);

            int run = -1;
            unsigned char top = bTop;
            unsigned char material = bMat;

            int gx = chunkX * 16 + x, gz = chunkZ * 16 + z;

            unsigned char col[WORLD_H];
            blockColumnGet(w, gx, gz, col);

            // The river channel. Structurally identical to the old moat
            // carve this biome used to have before the mushroom island
            // moved into real ocean instead of manufacturing its own water
            // ring -- same "only ever removes material" rule, so a river
            // crossing water that is already there changes nothing and no
            // river can end up standing proud of the surrounding sea --
            // but with a bed that varies across the channel instead of a
            // fixed one, which is what gives the water a sloped bottom
            // rather than a flat trough with square sides.
            if (riverT > 0.0f) {
                // Find the current top of the column. A seam that has
                // climbed above RIVER_CUT_CEILING_Y is left alone -- see
                // the constant's comment for why stopping beats gorging.
                int topY = -1;
                for (int y = WORLD_H - 1; y >= 0; y--) {
                    if (col[y] == BLOCK_STONE) { topY = y; break; }
                }

                if (topY >= 0 && topY <= RIVER_CUT_CEILING_Y) {
                    int span = RIVER_BED_BANK_Y - RIVER_BED_CENTRE_Y;
                    int bedY = RIVER_BED_BANK_Y - (int)(riverT * span + 0.5f);

                    for (int y = WORLD_H - 1; y > bedY; y--) {
                        if (y >= waterHeight) {
                            if (col[y] != BLOCK_AIR) col[y] = BLOCK_AIR;
                        } else if (col[y] == BLOCK_STONE) {
                            col[y] = BLOCK_CALM_WATER;
                        }
                    }
                }
            }

            for (int y = 127; y >= 0; y--) {
                unsigned char* cell = &col[y];

                if (y <= random.nextInt(5)) {
                    *cell = BLOCK_BEDROCK;
                } else {
                    unsigned char old = *cell;
                    if (old == BLOCK_AIR) {
                        run = -1;
                    } else if (old == BLOCK_STONE) {
                        if (run == -1) {
                            if (runDepth <= 0) {
                                top = BLOCK_AIR;
                                material = BLOCK_STONE;
                            } else if (y >= waterHeight - 4 && y <= waterHeight + 1) {
                                top = bTop; material = bMat;
                                // No sand or gravel beaches on the mushroom
                                // island: vanilla mushroom fields run
                                // mycelium right down to the waterline, and
                                // a sand rim would read as an ordinary
                                // island rather than a mushroom one.
                                if (biome != B_MUSHROOM) {
                                    if (gravel) { top = BLOCK_AIR;  material = BLOCK_GRAVEL; }
                                    if (sand)   { top = BLOCK_SAND; material = BLOCK_SAND; }
                                }
                            }
                            if (y < waterHeight && top == BLOCK_AIR) {
                                top = (temp < 0.15f) ? BLOCK_ICE : BLOCK_CALM_WATER;
                            }
                            run = runDepth;
                            *cell = (y >= waterHeight - 1) ? top : material;
                        } else if (run > 0) {
                            run--;
                            *cell = material;
                            if (run == 0 && material == BLOCK_SAND) {
                                run = random.nextInt(4);
                                material = BLOCK_SANDSTONE;
                            }
                        }
                    }
                }
            }
            blockColumnPut(w, gx, gz, col);
        }
    }
}

static int      g_genMask = 0;

bool McpeGen::postProcessPhase(World* w, int chunkX, int chunkZ, int phase) {
    int xo = chunkX * 16, zo = chunkZ * 16;
    switch (phase) {
    case 0: {
    computeBiome(chunkX, chunkZ);
    mPhaseBiome = (int)classifyBiomeSpatial(worldSeed, w, xo + 8, zo + 8);

    // Jungle generation is ENABLED. It was previously redirected to forest
    // here while the vine bugs were outstanding; that redirect is gone.
    // mPhaseBiome is set once in phase 0 and read by the decoration phases
    // below, so this one assignment was the only gate -- biomeSurface()
    // treats jungle exactly like forest (grass over dirt), and the two
    // classifyBiomeSpatialEx() callers earlier only branch on B_MUSHROOM
    // and river valleys, so nothing else needed unblocking.

    random.setSeed(worldSeed);
    int xScale = random.nextInt() / 2 * 2 + 1;
    int zScale = random.nextInt() / 2 * 2 + 1;
    unsigned int h = (unsigned int)chunkX * (unsigned int)xScale + (unsigned int)chunkZ * (unsigned int)zScale;
    random.setSeed((long)(int)(h ^ (unsigned int)worldSeed));

    for (int i = 0; i < 10; i++) {
        int x = xo + random.nextInt(16), y = random.nextInt(128), z = zo + random.nextInt(16);
        clayFeature(w, random, x, y, z);
    }

    return false; }

    case 1: {

    for (int i = 0; i < 20; i++) { int x = xo + random.nextInt(16), y = random.nextInt(128), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_DIRT, 32); }
    for (int i = 0; i < 10; i++) { int x = xo + random.nextInt(16), y = random.nextInt(128), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_GRAVEL, 32); }
    for (int i = 0; i < 20; i++) { int x = xo + random.nextInt(16), y = random.nextInt(128), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_ORE_COAL, 16); }
    for (int i = 0; i < 20; i++) { int x = xo + random.nextInt(16), y = random.nextInt(64), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_ORE_IRON, 8); }
    for (int i = 0; i < 2; i++) { int x = xo + random.nextInt(16), y = random.nextInt(32), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_ORE_GOLD, 8); }
    for (int i = 0; i < 8; i++) { int x = xo + random.nextInt(16), y = random.nextInt(16), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_ORE_REDSTONE, 7); }
    // Diamond and lapis attempt counts doubled (1 -> 2 per chunk each).
    // Vanilla's real per-chunk counts are coal 20, iron 20, gold 2,
    // redstone 8, diamond 1, lapis 1 -- this generator already matches
    // that closely everywhere else, and diamond/lapis being stuck at 1
    // attempt/chunk is a deliberate vanilla scarcity choice, not a bug.
    // Doubling keeps them clearly rarer than gold (2) and far rarer than
    // coal/iron (20), while still being noticeably more frequent than
    // before -- a plain, easily-adjusted multiplier rather than an
    // arbitrary-looking number. Vein size (7 / 6) and Y-range are
    // unchanged; this only changes how often a vein is attempted, same
    // as the quartz fix above.
    for (int i = 0; i < 2; i++) { int x = xo + random.nextInt(16), y = random.nextInt(16), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_ORE_DIAMOND, 7); }
    for (int i = 0; i < 2; i++) { int x = xo + random.nextInt(16), y = random.nextInt(16) + random.nextInt(16), z = zo + random.nextInt(16); oreFeature(w, random, x, y, z, BLOCK_ORE_LAPIS, 6); }

    return false; }

    case 2: {
    BiomeId biome = (BiomeId)mPhaseBiome;

    float fss = 0.5f;
    int oFor = (int)((forestNoise.getValue(xo * fss, zo * fss) / 8 + random.nextFloat() * 4 + 4) / 3);
    int forests = 0;
    if (random.nextInt(10) == 0) forests += 1;
    if (biome == B_FOREST)   forests += oFor + 2;
    if (biome == B_RAIN)     forests += oFor + 2;
    // Jungle: denser than the plain forests, and denser than it used to be
    // (was oFor + 3). About a third of these come out as short oaks filling
    // the understorey under the tall jungle canopy, so the extra rolls buy
    // a layered forest rather than just more of the same tree.
    if (biome == B_JUNGLE)   forests += oFor + 8;
    if (biome == B_SEASONAL) forests += oFor + 1;
    if (biome == B_TAIGA)    forests += oFor + 1;
    // Dark forest: the canopy is meant to be a continuous roof -- thick
    // enough to walk across, to black out the forest floor, and so to let
    // hostiles spawn under it in daylight. That needs far more trees than
    // the other forest biomes, not slightly more.
    //
    // Monte Carlo over a 3x3 chunk area, canopy modelled as the r=3/4/4/3/2
    // blob stack treeDarkOak actually builds, rejecting a tree when a log
    // is already within 3 blocks (leaves do not block placement --
    // isSolidGen excludes them -- so canopies interpenetrate freely and
    // only trunks compete):
    //
    //   attempts/chunk    trees planted    sky covered
    //          6 (old)          4.3            66%
    //         12                6.7            85%
    //         18                7.1            88%
    //         24                8.7            95%
    //         30                9.4            98%
    //
    // Trunk collisions cap planted trees around 9-10 per chunk, so past
    // ~24 attempts the extra rolls buy almost nothing.
    //
    // The vertex cost is far below what 4x the trees suggests, because
    // mesh_block.cpp:432 culls leaf-against-leaf whenever leavesCull or
    // leavesOpaque is set (i.e. always, unless fancy graphics AND fancy
    // leaves are both on). Merged canopies therefore render one shared
    // shell rather than one shell per tree: exposed leaf faces per chunk
    // go 678 -> 1006 from 6 to 24 attempts, about 6k verts in layer 2
    // against a 65536 scratch cap. Beyond 18 attempts it is flat.
    if (biome == B_DARK_FOREST) forests += oFor + 22;
    if (biome == B_DESERT)   forests -= 20;
    if (biome == B_TUNDRA)   forests -= 20;
    if (biome == B_PLAINS)   forests -= 20;
    // Mushroom fields have no ordinary trees at all -- huge mushrooms take
    // their place, placed in their own loop below. Same -20 idiom the
    // treeless biomes above already use.
    if (biome == B_MUSHROOM) forests -= 20;
    for (int i = 0; i < forests; i++) {
        int tx = xo + random.nextInt(16) + 8, tz = zo + random.nextInt(16) + 8;
        int ty = heightmapAt(w, tx, tz);

        if (biome == B_TAIGA) {
            if (random.nextInt(3) == 0) treePine(w, random, tx, ty, tz);
            else                        treeSpruce(w, random, tx, ty, tz);
        } else if (biome == B_FOREST) {
            if (random.nextInt(5) == 0) treeBirch(w, random, tx, ty, tz);
            else { random.nextInt(3); treeOak(w, random, tx, ty, tz); }
        } else if (biome == B_RAIN) {
            random.nextInt(3);
            treeOak(w, random, tx, ty, tz);
        } else if (biome == B_JUNGLE) {
            // Roughly one tree in three is an ordinary short oak. Jungle
            // trees are tall with the canopy held high, so a jungle built
            // only from them has an empty, walkable understorey; the short
            // oaks fill that lower layer and give the vines something to
            // hang on at eye level.
            //
            // Both kinds get their trunk sheathed. treeJungle does its own
            // columns internally (it knows whether it built a 1x1 or a 2x2
            // trunk); the oak is coated here, since treeOak is shared with
            // every other biome and must not start growing vines in them.
            //
            // vineCoatTrunk walks upward while the column is still log, so
            // it does not need to be told the tree's height, and it is a
            // no-op if treeOak declined to build (no log at ty).
            random.nextInt(3);
            if (random.nextInt(3) == 0) {
                treeOak(w, random, tx, ty, tz);
                vineCoatTrunk(w, random, tx, ty, tz);
            } else {
                treeJungle(w, random, tx, ty, tz);
            }
        } else if (biome == B_DARK_FOREST) {
            // Dark oaks with the occasional ordinary oak mixed in (wiki:
            // "Oak, dark oak, and sometimes birch trees generate in these
            // biomes"). Birch is left out here even though the wiki
            // mentions it -- treeBirch's 1-wide single trunk would read as
            // sparse/thin against dark oak's thick 2x2 trunks and hurt the
            // "dense, sky-blocking canopy" read this biome is going for.
            random.nextInt(3);
            if (random.nextInt(5) == 0) treeOak(w, random, tx, ty, tz);
            else                        treeDarkOak(w, random, tx, ty, tz);
        } else {
            random.nextInt(10);
            treeOak(w, random, tx, ty, tz);
        }
    }

    if (biome == B_MUSHROOM) {
        // Huge mushrooms are this biome's canopy. Placed after the tree
        // loop rather than inside it because they are not trees: they take
        // no part in the forestNoise density that drives tree count, and
        // their own generators do their own clearance and ground checks
        // (grass/dirt/mycelium -- see feature_mushroom_huge.cpp).
        //
        // Note this loop only draws from `random` when the biome actually
        // is mushroom, so adding it does not shift the random stream for
        // any other biome's chunks.
        int huge = 3 + random.nextInt(4);
        for (int i = 0; i < huge; i++) {
            int tx = xo + random.nextInt(16) + 8, tz = zo + random.nextInt(16) + 8;
            int ty = heightmapAt(w, tx, tz);
            if (random.nextInt(2) == 0) mushroomHugeRed(w, random, tx, ty, tz);
            else                        mushroomHugeBrown(w, random, tx, ty, tz);
        }
    }

    if (biome == B_DARK_FOREST) {
        // Huge mushrooms are a rare landmark here, not scenery. The old
        // random.nextInt(3) meant 0/1/2 attempts per chunk -- an average
        // of one per chunk, so on the order of eighty across a dark forest
        // region on the 512 preset. They read as a mushroom field with
        // trees in it rather than a forest with the odd mushroom.
        //
        // One attempt in DARK_FOREST_MUSHROOM_ODDS chunks instead. A
        // Voronoi region is roughly 1/13 of the world, so:
        //
        //   512 preset  (1024 chunks) -> ~79 chunks/biome  -> ~2 mushrooms
        //   1024 preset (4096 chunks) -> ~315 chunks/biome -> ~8 mushrooms
        //
        // and the real count comes in under that, because
        // hugeMushroomSpaceClear still rejects spots the dense canopy has
        // already filled. Tuned for the 512 default; this is the knob to
        // turn if the 1024 preset feels crowded.
        const int DARK_FOREST_MUSHROOM_ODDS = 40;
        if (random.nextInt(DARK_FOREST_MUSHROOM_ODDS) == 0) {
            int tx = xo + random.nextInt(16) + 8, tz = zo + random.nextInt(16) + 8;
            int ty = heightmapAt(w, tx, tz);
            if (random.nextInt(5) < 3) mushroomHugeRed(w, random, tx, ty, tz);   // red more common
            else                       mushroomHugeBrown(w, random, tx, ty, tz);
        }
    }

    return false; }

    case 3: {
    BiomeId biome = (BiomeId)mPhaseBiome;

    // Mushroom fields grow no flowers and no grass in vanilla -- the
    // ground cover is small mushrooms, and lots of them. Branching here
    // rather than scaling the counts keeps every other biome's draw
    // sequence from `random` byte-identical to what it was.
    if (biome == B_MUSHROOM) {
        for (int i = 0; i < 12; i++) {
            int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8;
            mushroomFeature(w, random, x, y, z,
                            (random.nextInt(2) == 0) ? BLOCK_MUSHROOM_BROWN : BLOCK_MUSHROOM_RED);
        }
    } else {

    for (int i = 0; i < 2; i++) { int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8; flowerFeature(w, random, x, y, z, BLOCK_FLOWER); }
    if (random.nextInt(2) == 0) { int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8; flowerFeature(w, random, x, y, z, BLOCK_ROSE); }

    for (int i = 0; i < 10; i++) {
        int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8;
        unsigned char grassData = (random.nextInt(2) == 0) ? TG_FERN : TG_TALL_GRASS;
        flowerFeature(w, random, x, y, z, BLOCK_TALLGRASS, grassData);
    }
    if (random.nextInt(4) == 0) { int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8; mushroomFeature(w, random, x, y, z, BLOCK_MUSHROOM_BROWN); }
    if (random.nextInt(8) == 0) { int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8; mushroomFeature(w, random, x, y, z, BLOCK_MUSHROOM_RED); }

    }

    for (int i = 0; i < 10; i++) { int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8; reedsFeature(w, random, x, y, z); }

    int cacti = (biome == B_DESERT) ? 5 : 0;
    for (int i = 0; i < cacti; i++) { int x = xo + random.nextInt(16) + 8, y = random.nextInt(128), z = zo + random.nextInt(16) + 8; cactusFeature(w, random, x, y, z); }

    if (biome == B_JUNGLE) {
        // Dense-looking ground cover is much cheaper than adding more full
        // jungle trees: these are tiny static bushes (up to 7 leaf writes),
        // no growth/AI/entity state. Kept at 5-7 attempts (not raised
        // alongside the fern count below) since this pass alone already
        // adds a heightmapAt scan plus up to ~10 block reads/writes per
        // attempt on top of everything else this phase already does for
        // every biome (flowers, grass, reeds) -- see the perf note in the
        // fern loop below before raising either count further.
        //
        // Placed before the fern loop, so it draws from `random` first --
        // same convention as the mushroom-biome branch above (see its
        // "does not shift the random stream for any other biome" note).
        // This only affects the draw sequence for B_JUNGLE chunks; every
        // other biome's sequence is untouched. It does mean any
        // not-yet-generated jungle chunk in an existing save will roll
        // different fern/bamboo positions than it would have before this
        // was added, since bushClusters now consumes random() calls ahead
        // of them -- already-generated chunks are unaffected, since
        // decoration only runs once per chunk at first generation.
        int bushClusters = 5 + random.nextInt(3);
        for (int i = 0; i < bushClusters; i++) {
            int x = xo + random.nextInt(16) + 8, z = zo + random.nextInt(16) + 8;
            int y = heightmapAt(w, x, z);
            jungleUnderstoryFeature(w, random, x, y, z);
        }

        // Left at the original 8-13 count. A prior patch proposal raised
        // this to 18 + random.nextInt(9) (18-26) at the same time it added
        // the bush pass above, which roughly doubles this loop's cost on
        // top of the new bush attempts -- undocumented at the time, so
        // left out here. Raise deliberately if the ground still reads too
        // sparse once the bushes are in, not as a side effect of adding
        // them.
        int fernClusters = 8 + random.nextInt(6);
        for (int i = 0; i < fernClusters; i++) {
            int x = xo + random.nextInt(16) + 8, z = zo + random.nextInt(16) + 8;
            int y = heightmapAt(w, x, z);
            unsigned char below = worldBlock(w, x, y - 1, z);
            if ((below != BLOCK_GRASS && below != BLOCK_DIRT) || worldBlock(w, x, y, z) != BLOCK_AIR) continue;
            setBlock(w, x, y, z, BLOCK_TALLGRASS, TG_FERN);
            if (worldBlock(w, x, y + 1, z) == BLOCK_AIR)
                setBlock(w, x, y + 1, z, BLOCK_TALLGRASS, TG_FERN | 8);
        }

        int bambooClusters = 3 + random.nextInt(3);
        for (int i = 0; i < bambooClusters; i++) {
            int x = xo + random.nextInt(16) + 8, z = zo + random.nextInt(16) + 8;
            int y = heightmapAt(w, x, z);
            unsigned char below = worldBlock(w, x, y - 1, z);
            if (below != BLOCK_GRASS && below != BLOCK_DIRT) continue;

            // Most clusters are freshly sprouted; some are already fully grown
            // when the world starts, so bamboo groves don't all look brand new.
            int stalkHeight = (random.nextInt(3) == 0) ? (8 + random.nextInt(5)) : 1;
            for (int hh = 0; hh < stalkHeight; hh++) {
                if (worldBlock(w, x, y + hh, z) != BLOCK_AIR) break;
                setBlock(w, x, y + hh, z, BLOCK_BAMBOO, 0);
            }
        }
    }

    return false; }

    case 4: {

    // Villages are generated after trees/ground cover so later vegetation
    // passes cannot accidentally grow trees on rooftops. The generator is
    // chunk-local and deterministic, so it remains safe for streaming.
    if (genFeatureEnabled(g_genMask, GEN_FEATURE_VILLAGES))
        villageGenerateChunk(w, worldSeed, chunkX, chunkZ);

    // Dungeons are generated after vegetation and villages, and entirely
    // inside one chunk. That keeps the structure deterministic and safe for
    // the streaming generator while allowing its spawner/chests to become
    // ordinary world tile entities.
    if (genFeatureEnabled(g_genMask, GEN_FEATURE_DUNGEONS))
        dungeonGenerateChunk(w, worldSeed, chunkX, chunkZ);

    #define SPRING_WATER_TRIES 50
    #define SPRING_LAVA_TRIES  20
    for (int i = 0; i < SPRING_WATER_TRIES; i++) {
        int x = xo + random.nextInt(16) + 8, y = random.nextInt(random.nextInt(120) + 8), z = zo + random.nextInt(16) + 8;
        springFeature(w, x, y, z, BLOCK_WATER);
    }
    for (int i = 0; i < SPRING_LAVA_TRIES; i++) {
        int x = xo + random.nextInt(16) + 8, y = random.nextInt(random.nextInt(random.nextInt(112) + 8) + 8), z = zo + random.nextInt(16) + 8;
        springFeature(w, x, y, z, BLOCK_LAVA);
    }

    return false; }

    default: {

    snowCap(w, chunkX, chunkZ, mTemp);
    return true; }
    }
}

static McpeGen* g_gen = 0;
static long     g_genSeed = 0;
void worldGenInit(long seed, int genMask) {
    if (g_gen && g_genSeed == seed) { g_genMask = genMask; return; }
    worldGenFree();
    g_gen = new (std::nothrow) McpeGen(seed);
    g_genSeed = seed;
    g_genMask = genMask;
}

void worldGenFree() {
    delete g_gen; g_gen = 0;
}

long worldGenSeed() {
    return g_genSeed;
}

int worldGenMask() {
    return g_genMask;
}

void chunkGenerateTerrain(World* w, int cx, int cz) {
    if (!g_gen) return;

    g_gen->random.setSeed((long)(int)((unsigned int)cx * 341872712u + (unsigned int)cz * 132899541u));
    g_gen->computeBiome(cx, cz);
    g_gen->prepareChunk(w, cx, cz);
    g_gen->buildSurfacesChunk(w, cx, cz);

    if (genFeatureEnabled(g_genMask, GEN_FEATURE_CAVES)) caveFeature(w, g_genSeed, cx, cz);
}

bool chunkPostProcessPhase(World* w, int cx, int cz, int phase) {
    if (!g_gen) return true;
    return g_gen->postProcessPhase(w, cx, cz, phase);
}

void worldGenerateMCPE(World* w, long seed, int genMask) {
    worldGenInit(seed, genMask);

    // A brand-new world has no Nether portal yet. Clearing here (rather
    // than relying on process lifetime) matters because creating a world
    // after having played another one in the same session would otherwise
    // inherit the previous world's anchor -- see the same reset on the
    // load path in LevelStorage::load.
    netherPortalResetAnchor();

    // Three cases:
    //  - World fits entirely in the resident window: generate exactly
    //    that (unchanged from before, just using the runtime sizeX/sizeZ
    //    instead of the old compile-time square WORLD_SIZE_CHUNKS).
    //  - Infinite world (sizeX == 0): touch a small window's worth of
    //    chunks around the origin so there's something for spawn-finding
    //    to search through immediately; the rest streams in lazily during
    //    play via worldStream, same as always.
    //  - Finite world too large to fit the window (the two pre-generated
    //    presets, 512x512 / 1024x1024): do nothing here. worldInitTerrain
    //    runs the real batch sweep (worldPreGenerateSweep) across the
    //    world's full logical bound right after this returns, which is a
    //    strict superset of touching just the origin window -- generating
    //    that small window here first would be pure wasted duplicate work
    //    the sweep immediately redoes and then evicts.
    if (worldFitsInWindow(w)) {
        const int sideX = w->sizeX, sideZ = w->sizeZ;
        int totalChunks = sideX * sideZ;
        int doneChunks = 0;
        for (int cz = 0; cz < sideZ; cz++)
        for (int cx = 0; cx < sideX; cx++) {
            worldGetChunk(w, cx, cz);
            doneChunks++;
            g_terrainProgress = (doneChunks * 50) / totalChunks;

            sceKernelDelayThread(100);
        }
    } else if (w->sizeX == 0) {
        int totalChunks = WORLD_CHUNKS_X * WORLD_CHUNKS_Z;
        int doneChunks = 0;
        for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++)
        for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
            worldGetChunk(w, cx, cz);
            doneChunks++;
            g_terrainProgress = (doneChunks * 50) / totalChunks;

            sceKernelDelayThread(100);
        }
    }
}

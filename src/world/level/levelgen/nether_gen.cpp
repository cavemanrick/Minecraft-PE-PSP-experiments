#include "world/level/levelgen/nether_gen.h"
#include "world/level/levelgen/nether_biome.h"
#include "world/level/levelgen/features.h"
#include "world/level/levelgen/Random.h"
#include "world/level/world.h"

#include <math.h>

// --- Vertical shell -----------------------------------------------------
// Classic Nether shape scaled to this engine's 128-tall column (vanilla's
// is also 128, y=0..127, so no rescaling is actually needed): a solid
// bedrock cap at the very top and bottom, netherrack shell in between,
// carved into open caverns, with a lava sea flooding everything below a
// fixed level. Kept as plain constants rather than derived from WORLD_H
// so the numbers stay readable and match vanilla's own fixed layout.
#define NETHER_BEDROCK_BOTTOM   0
#define NETHER_BEDROCK_TOP      127
#define NETHER_FLOOR_Y          1     // netherrack starts here (above bottom bedrock)
#define NETHER_CEILING_Y        126   // netherrack ends here (below top bedrock)
#define NETHER_LAVA_LEVEL       31    // everything at/below this, within the shell, is lava

#define MCPE_PI 3.14159265f

// --- Bulk shell fill ------------------------------------------------------

static void netherFillColumn(World* w, int cx, int cz) {
    int xo = cx * 16, zo = cz * 16;
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            int gx = xo + x, gz = zo + z;
            for (int y = NETHER_BEDROCK_BOTTOM; y <= NETHER_BEDROCK_TOP; y++) {
                unsigned char id;
                if (y == NETHER_BEDROCK_BOTTOM || y == NETHER_BEDROCK_TOP) {
                    id = BLOCK_BEDROCK;
                } else if (y < NETHER_FLOOR_Y || y > NETHER_CEILING_Y) {
                    id = BLOCK_AIR; // shouldn't happen given the constants above, kept for safety
                } else if (y <= NETHER_LAVA_LEVEL) {
                    id = BLOCK_CALM_LAVA;
                } else {
                    id = BLOCK_NETHERRACK;
                }
                blockPut(w, gx, y, gz, id);
            }
        }
    }
}

// --- Cavern carving ---------------------------------------------------
// Directly reuses caves.cpp's tunnel-carving math (same ellipsoid-cross-
// -section, same recursive branch-split shape) rather than duplicating a
// second copy of it. The overworld version replaces BLOCK_STONE/DIRT/
// GRASS with air (or lava below a cutoff); this version replaces
// BLOCK_NETHERRACK and BLOCK_CALM_LAVA with air instead, since Nether
// caverns should open up the lava sea into breathable pockets too, not
// just the netherrack above it -- that's what makes the "heavily carved,
// open caverns" shape actually walkable near the lava level rather than
// just riddling the upper netherrack with holes. Radii are scaled up
// relative to the overworld version (see NETHER_CAVE_RADIUS_MUL) for the
// "large open caverns" brief rather than tight crawl-tunnels.
#define NETHER_CAVE_RADIUS_MUL 2.2f

static void netherCaveAddTunnel(World* w, Random& parentRandom, int xOffs, int zOffs,
                                 float xCave, float yCave, float zCave,
                                 float thickness, float yRot, float xRot,
                                 int step, int dist, float yScale) {
    float xMid = (float)(xOffs * 16 + 8);
    float zMid = (float)(zOffs * 16 + 8);

    float yRota = 0, xRota = 0;
    Random random(parentRandom.nextLong());

    if (dist <= 0) {
        int maxDist = 8 * 16 - 16;
        dist = maxDist - random.nextInt(maxDist / 4);
    }
    bool singleStep = false;
    if (step == -1) { step = dist / 2; singleStep = true; }

    int splitPoint = random.nextInt(dist / 2) + dist / 4;
    bool steep = random.nextInt(6) == 0;

    for (; step < dist; step++) {
        float rad = (1.5f + sinf(step * MCPE_PI / dist) * thickness) * NETHER_CAVE_RADIUS_MUL;
        float yRad = rad * yScale;

        float xc = cosf(xRot), xs = sinf(xRot);
        xCave += cosf(yRot) * xc;
        yCave += xs;
        zCave += sinf(yRot) * xc;

        xRot *= steep ? 0.92f : 0.7f;
        xRot += xRota * 0.1f;
        yRot += yRota * 0.1f;
        xRota *= 0.90f; yRota *= 0.75f;
        xRota += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2;
        yRota += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4;

        if (!singleStep && step == splitPoint && thickness > 1) {
            netherCaveAddTunnel(w, random, xOffs, zOffs, xCave, yCave, zCave,
                                random.nextFloat() * 0.5f + 0.5f, yRot - MCPE_PI / 2, xRot / 3, step, dist, 1.0f);
            netherCaveAddTunnel(w, random, xOffs, zOffs, xCave, yCave, zCave,
                                random.nextFloat() * 0.5f + 0.5f, yRot + MCPE_PI / 2, xRot / 3, step, dist, 1.0f);
            return;
        }
        if (!singleStep && random.nextInt(4) == 0) continue;

        {
            float xd = xCave - xMid, zd = zCave - zMid;
            float remaining = (float)(dist - step);
            float rr = (thickness + 2) + 16;
            if (xd * xd + zd * zd - (remaining * remaining) > rr * rr) return;
        }

        if (xCave < xMid - 16 - rad * 2 || zCave < zMid - 16 - rad * 2 ||
            xCave > xMid + 16 + rad * 2 || zCave > zMid + 16 + rad * 2) continue;

        int x0 = (int)floorf(xCave - rad) - xOffs * 16 - 1;
        int x1 = (int)floorf(xCave + rad) - xOffs * 16 + 1;
        int y0 = (int)floorf(yCave - yRad) - 1;
        int y1 = (int)floorf(yCave + yRad) + 1;
        int z0 = (int)floorf(zCave - rad) - zOffs * 16 - 1;
        int z1 = (int)floorf(zCave + rad) - zOffs * 16 + 1;

        // Clamp within the netherrack shell (never touch the bedrock caps).
        if (x0 < 0) x0 = 0;
        if (x1 > 16) x1 = 16;
        if (y0 < NETHER_FLOOR_Y) y0 = NETHER_FLOOR_Y;
        if (y1 > NETHER_CEILING_Y) y1 = NETHER_CEILING_Y;
        if (z0 < 0) z0 = 0;
        if (z1 > 16) z1 = 16;

        for (int xx = x0; xx < x1; xx++) {
            float xd = ((xx + xOffs * 16 + 0.5f) - xCave) / rad;
            for (int zz = z0; zz < z1; zz++) {
                float zd = ((zz + zOffs * 16 + 0.5f) - zCave) / rad;
                if (xd * xd + zd * zd >= 1) continue;
                int gx = xOffs * 16 + xx, gz = zOffs * 16 + zz;
                for (int yy = y1 - 1; yy >= y0; yy--) {
                    float yd = (yy + 0.5f - yCave) / yRad;
                    if (yd > -0.7f && xd * xd + yd * yd + zd * zd < 1) {
                        unsigned char block = worldBlock(w, gx, yy, gz);
                        if (block == BLOCK_NETHERRACK || block == BLOCK_CALM_LAVA || block == BLOCK_LAVA) {
                            blockPut(w, gx, yy, gz, BLOCK_AIR);
                        }
                    }
                }
            }
        }
        if (singleStep) break;
    }
}

static void netherCaveAddRoom(World* w, Random& random, int xOffs, int zOffs,
                               float xRoom, float yRoom, float zRoom) {
    netherCaveAddTunnel(w, random, xOffs, zOffs, xRoom, yRoom, zRoom,
                        1 + random.nextFloat() * 6, 0, 0, -1, -1, 0.5f);
}

// Denser than the overworld's CAVE_RARITY (1-in-15 chunks) -- "heavily
// carved, large open caverns" means most Nether chunks should have some
// cavern passing through them, not the occasional isolated pocket.
#define NETHER_CAVE_RARITY 3

static void netherCaveAddFeature(World* w, Random& random, int x, int z, int xOffs, int zOffs) {
    int caves = random.nextInt(random.nextInt(random.nextInt(40) + 1) + 1) + 1;
    if (random.nextInt(NETHER_CAVE_RARITY) != 0) caves = 0;

    for (int cave = 0; cave < caves; cave++) {
        float xCave = (float)(x * 16 + random.nextInt(16));
        float yCave = (float)(NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y));
        float zCave = (float)(z * 16 + random.nextInt(16));

        int tunnels = 1;
        if (random.nextInt(3) == 0) {
            netherCaveAddRoom(w, random, xOffs, zOffs, xCave, yCave, zCave);
            tunnels += random.nextInt(4);
        }
        for (int i = 0; i < tunnels; i++) {
            float yRot = random.nextFloat() * MCPE_PI * 2;
            float xRot = ((random.nextFloat() - 0.5f) * 2) / 8;
            float thickness = random.nextFloat() * 2 + random.nextFloat();
            netherCaveAddTunnel(w, random, xOffs, zOffs, xCave, yCave, zCave, thickness, yRot, xRot, 0, 0, 1.0f);
        }
    }
}

static void netherCarveCaverns(World* w, long worldSeed, int chunkX, int chunkZ) {
    Random random(worldSeed ^ 0x4E43415645L /* "NCAVE" */);
    long xScale = random.nextLong() / 2 * 2 + 1;
    long zScale = random.nextLong() / 2 * 2 + 1;

    const int r = 8;
    for (int x = chunkX - r; x <= chunkX + r; x++) {
        for (int z = chunkZ - r; z <= chunkZ + r; z++) {
            random.setSeed((x * xScale + z * zScale) ^ worldSeed);
            netherCaveAddFeature(w, random, x, z, chunkX, chunkZ);
        }
    }
}

// --- Per-biome decoration -----------------------------------------------
// Runs after carving, so decoration only ever lands on cavern surfaces
// (netherrack exposed to an air pocket) rather than being buried inside
// solid shell that never got carved out.

static bool isNetherrackFace(World* w, int x, int y, int z) {
    return worldBlock(w, x, y, z) == BLOCK_NETHERRACK;
}

static void decorateWastes(World* w, Random& random, int xo, int zo) {
    // Glowstone clusters hanging from cavern ceilings -- the one Nether
    // Wastes light source already fully supported by the tile/render
    // layer (see tile.cpp), so this needs no new block work at all.
    int clusters = 2 + random.nextInt(3);
    for (int i = 0; i < clusters; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y);
        // A ceiling spot: air here, netherrack directly above.
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (!isNetherrackFace(w, x, y + 1, z)) continue;
        int blobSize = 3 + random.nextInt(5);
        for (int b = 0; b < blobSize; b++) {
            int bx = x + random.nextInt(3) - 1, bz = z + random.nextInt(3) - 1;
            if (worldBlock(w, bx, y, bz) == BLOCK_AIR && isNetherrackFace(w, bx, y + 1, bz))
                setBlock(w, bx, y, bz, BLOCK_GLOWSTONE);
        }
    }

    // Magma blocks scattered on cavern floors near the lava sea, same
    // rough placement vanilla uses (small blobs just above the lava
    // line) -- purely decorative here (no lava-damage/bubble-column
    // behavior implemented), but it reads correctly and now has a real
    // texture (see tile.cpp's BLOCK_MAGMA case).
    int magmaBlobs = 1 + random.nextInt(2);
    for (int i = 0; i < magmaBlobs; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_LAVA_LEVEL + 1 + random.nextInt(6);
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
            for (int y = NETHER_CEILING_Y; y >= NETHER_FLOOR_Y; y--) {
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
            for (int y = NETHER_CEILING_Y; y >= NETHER_FLOOR_Y; y--) {
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
        int y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_FUNGUS);
    }

    int wartTries = 2 + random.nextInt(3);
    for (int i = 0; i < wartTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y);
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
        int y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y);
        if (worldBlock(w, x, y, z) != BLOCK_AIR) continue;
        if (worldBlock(w, x, y - 1, z) != BLOCK_WARPED_NYLIUM) continue;
        setBlock(w, x, y, z, BLOCK_WARPED_ROOTS);
    }

    int sproutTries = 2 + random.nextInt(3);
    for (int i = 0; i < sproutTries; i++) {
        int x = xo + random.nextInt(16), z = zo + random.nextInt(16);
        int y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y);
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
        int y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y);
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

// --- Entry point ----------------------------------------------------------

void chunkGenerateNether(World* w, long worldSeed, int cx, int cz) {
    int xo = cx * 16, zo = cz * 16;

    netherFillColumn(w, cx, cz);
    netherCarveCaverns(w, worldSeed, cx, cz);

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

    // Quartz veins: now uses the real BLOCK_NETHER_QUARTZ_ORE id (see
    // chunk.h/tile.cpp) instead of the first pass's BLOCK_QUARTZ_BLOCK
    // stand-in. Present in Wastes and Soul Sand Valley, matching vanilla's
    // own distribution (quartz doesn't generate in Warped Forest).
    if (biome == NB_WASTES || biome == NB_SOUL_SAND_VALLEY) {
        int veins = 1 + random.nextInt(3);
        for (int i = 0; i < veins; i++) {
            int x = xo + random.nextInt(16), y = NETHER_FLOOR_Y + random.nextInt(NETHER_CEILING_Y - NETHER_FLOOR_Y), z = zo + random.nextInt(16);
            netherOreFeature(w, random, x, y, z, BLOCK_NETHER_QUARTZ_ORE, 12);
        }
    }
}

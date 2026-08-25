#include "world/entity/fishing_bobber.h"
#include "world/entity/entity_types.h"
#include "world/entity/entity_renderer_id.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "client/renderer/particle.h"
#include "util/mth.h"
#include <cmath>

static const float RAD = 180.0f / Mth::PI;
static const float DEG = Mth::PI / 180.0f;

// Bite timing, in ticks (20 = 1 second). Vanilla waits 5-30s; this is the
// same shape but a little shorter at the top end, because a PSP session is
// usually not one where standing still for half a minute reads as fun.
#define BITE_WAIT_MIN   100   // 5s
#define BITE_WAIT_EXTRA 300   // + up to 15s

// How long the "fish on" window lasts. This is the reaction time the player
// actually gets, and it is the single number to change if fishing feels too
// twitchy or too forgiving.
#define BITE_WINDOW_TICKS 30  // 1.5s

// Hard cap on a single cast, so a bobber left in water while the player
// wanders off does not live forever. FishingRodItem also cuts the line on
// distance and on deselecting the rod; this is the backstop for anything
// those two miss.
#define BOBBER_MAX_LIFE (120 * TicksPerSecond)

void FishingBobber::init() {
    setSize(0.25f, 0.25f);
    entityRendererId = ER_FISHING_BOBBER_RENDERER;
    // Not solid to anything: the bobber should never push a mob, block a
    // placement, or stop an arrow.
    blocksBuilding = false;
    state = ST_FLYING;
    life = 0;
    biteTimer = 0;
    biteWindow = 0;
}

FishingBobber::FishingBobber(Level* level) : super(level) { init(); }

FishingBobber::FishingBobber(Level* level, float px, float py, float pz,
                             float yaw, float pitch) : super(level) {
    init();

    // Same muzzle offset and launch maths Throwable uses, so a cast lands
    // where the crosshair says it will rather than needing its own tuning.
    float cy = cosf(yaw * DEG),   sy = sinf(yaw * DEG);
    float cp = cosf(pitch * DEG), sp = sinf(pitch * DEG);

    setPos(px - cy * 0.16f, py - 0.1f, pz - sy * 0.16f);
    xOld = x; yOld = y; zOld = z;

    float dx = cp * sy, dy = sp, dz = cp * cy;
    float len = Mth::sqrt(dx * dx + dy * dy + dz * dz);
    if (len >= 0.001f) { dx /= len; dy /= len; dz /= len; }

    const float power = 1.2f;
    xd = dx * power; yd = dy * power; zd = dz * power;
    yRot = yRotO = atan2f(xd, zd) * RAD;
    xRot = xRotO = atan2f(yd, Mth::sqrt(xd * xd + zd * zd)) * RAD;
}

void FishingBobber::startWaitingForBite() {
    state = ST_FLOATING;
    biteWindow = 0;
    biteTimer = BITE_WAIT_MIN + sharedRandom.nextInt(BITE_WAIT_EXTRA);
}

void FishingBobber::tick() {
    xOld = x; yOld = y; zOld = z;
    baseTick();

    if (++life >= BOBBER_MAX_LIFE) { remove(); return; }

    if (state == ST_FLYING) {
        float nx = x + xd, ny = y + yd, nz = z + zd;

        // Tile collision only -- entities are deliberately ignored. Vanilla
        // lets you hook a mob and drag it; that needs a whole tug-of-war
        // system on top of the catch logic, so it is left out rather than
        // half-implemented, and the bobber simply passes through mobs.
        BlockHit tileHit = worldClip(level->w, x, y, z, nx, ny, nz, false, true);
        if (tileHit.hit) {
            // Landed on solid ground. Stick there and start no timer: a
            // bobber out of water never bites, which is exactly the
            // feedback the player needs that they cast onto the shore.
            x = tileHit.x + tileHit.clickX;
            y = tileHit.y + tileHit.clickY;
            z = tileHit.z + tileHit.clickZ;
            xd = yd = zd = 0.0f;
            setPos(x, y, z);
            return;
        }

        x = nx; y = ny; z = nz;
        setPos(x, y, z);

        if (isInWater()) {
            // Splashdown. Kill the horizontal run so the bobber settles
            // where it hit rather than skating across the surface.
            xd = zd = 0.0f;
            yd = 0.0f;
            level->playSound(this, "random.splash", 0.4f,
                             1.0f + sharedRandom.nextFloat() * 0.3f);
            startWaitingForBite();
            return;
        }

        xd *= 0.99f; yd *= 0.99f; zd *= 0.99f;
        yd -= 0.03f;

        float sd = Mth::sqrt(xd * xd + zd * zd);
        yRot = atan2f(xd, zd) * RAD;
        xRot = atan2f(yd, sd) * RAD;
        return;
    }

    // --- In water from here on ---

    // Buoyancy: push up while submerged, sink while not, and damp hard so
    // the bobber settles into a slow bob at the surface instead of
    // oscillating. Cheaper and steadier than sampling the exact water
    // height, and it self-corrects if the water level changes underneath.
    if (isInWater()) yd += 0.02f;
    else             yd -= 0.02f;
    yd *= 0.75f;
    y += yd;
    setPos(x, y, z);

    if (state == ST_FLOATING) {
        // Idle ripples, so a floating bobber reads as alive rather than
        // stuck. Sparse on purpose: this ticks 20x a second and the PSP
        // particle budget is shared with everything else on screen.
        if ((life % 10) == 0)
            particlesSuspended(x + (sharedRandom.nextFloat() - 0.5f) * 0.4f, y,
                               z + (sharedRandom.nextFloat() - 0.5f) * 0.4f);

        if (--biteTimer <= 0) {
            state = ST_BITING;
            biteWindow = BITE_WINDOW_TICKS;
            // The tell: bobber yanked under, a burst of splash, and a
            // sound. All three fire together because on a small screen in
            // motion any one of them alone is easy to miss.
            yd = -0.35f;
            level->playSound(this, "random.splash", 0.6f,
                             1.4f + sharedRandom.nextFloat() * 0.4f);
            for (int i = 0; i < 8; i++)
                particlesSplash(x + (sharedRandom.nextFloat() - 0.5f) * 0.5f, y + 0.1f,
                                z + (sharedRandom.nextFloat() - 0.5f) * 0.5f,
                                (sharedRandom.nextFloat() - 0.5f) * 0.2f,
                                sharedRandom.nextFloat() * 0.2f,
                                (sharedRandom.nextFloat() - 0.5f) * 0.2f);
        }
        return;
    }

    // ST_BITING
    for (int i = 0; i < 2; i++)
        particlesBubble(x + (sharedRandom.nextFloat() - 0.5f) * 0.3f, y,
                        z + (sharedRandom.nextFloat() - 0.5f) * 0.3f, 0.0f, 0.02f, 0.0f);

    if (--biteWindow <= 0) {
        // Missed it. Back to waiting rather than ending the cast -- see the
        // class comment.
        startWaitingForBite();
    }
}

int FishingBobber::getEntityTypeId() const { return EntityTypes::IdFishingBobber; }

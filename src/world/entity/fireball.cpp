#include "world/entity/fireball.h"
#include "world/entity/entity_types.h"
#include "world/entity/entity_renderer_id.h"
#include "world/entity/local_player.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "nbt/compound_tag.h"
#include "client/player/player_state.h"
#include "util/mth.h"
#include <cmath>

static const float RAD = 180.0f / Mth::PI;

// Blast power passed to worldExplode. Vanilla's fireball ExplosionPower
// defaults to 1, versus TNT's 4 and creeper's 3; this codebase's own
// worldExplode calls for those two are 3.1 and 2.4 respectively (see
// primed_tnt.cpp, creeper.cpp), so 1.0 keeps the same rough proportion
// (about a quarter of TNT) rather than copying vanilla's raw number into
// a differently-scaled r parameter.
static const float FIREBALL_EXPLOSION_POWER = 1.0f;

void Fireball::configure() {
    setSize(1.0f, 1.0f);
    entityRendererId = ER_FIREBALL_RENDERER;
    fireImmune = true;
}

Fireball::Fireball(Level* level)
    : super(level), ownerId(0), deflectedByPlayer(false), life(0) {
    configure();
}

Fireball::Fireball(Level* level, Entity* owner, float px, float py, float pz,
                     float dx, float dy, float dz, float speed)
    : super(level), ownerId(owner ? owner->entityId : 0),
      deflectedByPlayer(false), life(0) {
    configure();

    float dist = Mth::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist >= 0.001f) { dx /= dist; dy /= dist; dz /= dist; }
    else { dx = 0.0f; dy = 0.0f; dz = 1.0f; }

    setPos(px, py, pz);
    xOld = x; yOld = y; zOld = z;

    xd = dx * speed; yd = dy * speed; zd = dz * speed;
    float sd = Mth::sqrt(xd * xd + zd * zd);
    yRot = yRotO = atan2f(xd, zd) * RAD;
    xRot = xRotO = atan2f(yd, sd) * RAD;
}

int Fireball::getEntityTypeId() const { return EntityTypes::IdFireball; }

// Deflection, not damage. A fireball has no health bar in vanilla; what
// "hurting" one actually does is knock it back the way it came and make
// it the attacker's shot instead of the ghast's. damage's value is
// deliberately unused -- unlike every other entity's hurt(), a fireball
// doesn't have degrees of being hit, only "redirected" or not.
bool Fireball::hurt(Entity* source, int /*damage*/) {
    if (removed) return false;

    xd = -xd; yd = -yd; zd = -zd;
    float sd = Mth::sqrt(xd * xd + zd * zd);
    yRot = atan2f(xd, zd) * RAD;
    xRot = atan2f(yd, sd) * RAD;

    if (source) {
        ownerId = source->entityId;
        if (source->isEntityType(EntityTypes::IdLocalPlayer)) deflectedByPlayer = true;
    }

    // Reset flight time isn't tracked separately the way Arrow does (no
    // "can't hit what just hit you" grace window) -- a deflected fireball
    // travelling back the way it came will only be near the ghast that
    // fired it, not the player who just hit it, so there's no risk of an
    // immediate self-re-trigger the way Arrow's ownerId exclusion guards
    // against for a bow's own shooter.
    return true;
}

void Fireball::addAdditonalSaveData(CompoundTag* tag) {
    tag->putInt("OwnerId", ownerId);
    tag->putBoolean("DeflectedByPlayer", deflectedByPlayer);
}

void Fireball::readAdditionalSaveData(CompoundTag* tag) {
    ownerId = tag->getInt("OwnerId");
    deflectedByPlayer = tag->getBoolean("DeflectedByPlayer");
}

void Fireball::tick() {
    xOld = x; yOld = y; zOld = z;
    baseTick();

    float nx = x + xd, ny = y + yd, nz = z + zd;
    BlockHit tileHit = worldClip(level->w, x, y, z, nx, ny, nz, false, true);
    bool hitBlock = tileHit.hit;
    if (hitBlock) {
        nx = tileHit.x + tileHit.clickX;
        ny = tileHit.y + tileHit.clickY;
        nz = tileHit.z + tileHit.clickZ;
    }
    float sxd = nx - x, syd = ny - y, szd = nz - z;
    float dist = Mth::sqrt(sxd * sxd + syd * syd + szd * szd);

    static EntityList candidates;
    level->getEntities(this, bb.expand(xd, yd, zd).grow(0.3f, 0.3f, 0.3f), candidates);
    Entity* hitEntity = 0;
    bool    hitPlayer = false;
    float   nearest = 0.0f;
    if (dist > 1e-6f) {
        float ux = sxd / dist, uy = syd / dist, uz = szd / dist;
        float t;
        for (size_t ei = 0; ei < candidates.size(); ei++) {
            Entity* e = candidates[ei];
            if (e->removed || !e->isPickable()) continue;
            // Unlike Arrow, there's no flightTime grace window here: a
            // ghast that just fired this shot is stationary and the
            // fireball starts outside its own hitbox (see the spawn
            // offset in Ghast::aiStep), so an unconditional owner
            // exclusion can't cause the shot to visibly pass through its
            // firer -- it just means the ghast never blocks its own shot,
            // which is correct: it's the target, not an obstacle.
            if (e->entityId == ownerId) continue;
            if (e->bb.grow(0.3f, 0.3f, 0.3f).clip(x, y, z, ux, uy, uz, dist, t) &&
                (!hitEntity || t < nearest)) { hitEntity = e; nearest = t; }
        }

        if (level->player && level->player->isAlive() && ownerId != level->player->entityId) {
            LocalPlayer* p = level->player;
            float pf = p->y - PLAYER_EYE;
            AABB pbb(p->x - PLAYER_W * 0.5f, pf, p->z - PLAYER_W * 0.5f,
                     p->x + PLAYER_W * 0.5f, pf + PLAYER_H, p->z + PLAYER_W * 0.5f);
            if (pbb.grow(0.3f, 0.3f, 0.3f).clip(x, y, z, ux, uy, uz, dist, t) &&
                (!hitEntity || t < nearest)) { hitEntity = (Entity*)p; hitPlayer = true; }
        }
    }

    if (hitEntity || hitBlock) {
        // Direct-impact damage before the blast: vanilla fireballs deal 6
        // impact damage on top of the explosion itself. hurtPlayer/
        // hitEntity->hurt() here is ordinary damage, NOT the deflection
        // overload -- that only fires when something hits the FIREBALL
        // (Fireball::hurt above), not when the fireball hits something
        // else.
        if (hitPlayer) level->player->hurt(this, 6);
        else if (hitEntity) hitEntity->hurt(this, 6);

        worldExplode(level->w, x, y, z, FIREBALL_EXPLOSION_POWER);

        // A ghast caught in its own deflected fireball's blast dies to
        // it via the normal worldExplode entity-damage pass above (10 HP
        // is well under any blast within a few blocks), which calls
        // Ghast::hurt with source == this Fireball. deflectedByPlayer is
        // read from there to award the achievement -- see ghast.cpp.
        remove();
        return;
    }

    x = nx; y = ny; z = nz;
    float sd = Mth::sqrt(xd * xd + zd * zd);
    yRot = atan2f(xd, zd) * RAD;
    xRot = atan2f(yd, sd) * RAD;
    setPos(x, y, z);

    // Deliberately no gravity and no inertia decay: vanilla fireballs
    // travel in a straight line, unaffected by gravity, at constant
    // speed. Arrow/Throwable both apply yd -= gravity and an inertia
    // multiplier every tick; a fireball does neither.

    if (++life >= 60 * TicksPerSecond) remove();
}

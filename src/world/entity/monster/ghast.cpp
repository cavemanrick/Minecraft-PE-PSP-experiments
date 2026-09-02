#include "world/entity/monster/ghast.h"
#include "world/entity/entity_types.h"
#include "world/entity/entity_renderer_id.h"
#include "world/entity/local_player.h"
#include "world/entity/fireball.h"
#include "world/entity/mob_category.h"
#include "world/level/level.h"
#include "world/level/world.h"
#include "world/level/chunk/chunk.h"
#include "world/item/item.h"
#include "world/achievement/achievement.h"
#include "nbt/compound_tag.h"
#include "util/mth.h"
#include <cmath>

// Ticks between shots once one has fired. The wiki gives "every 3
// seconds"; TicksPerSecond is this codebase's own tick-rate constant, the
// same one Arrow/Fireball's 60*TicksPerSecond despawn timers use.
#define GHAST_ATTACK_COOLDOWN (3 * TicksPerSecond)

// Wind-up before a committed shot actually leaves -- long enough for the
// shooting-face texture swap to read as a real warning tell rather than
// an instant snap from idle to fired.
#define GHAST_CHARGE_TICKS (TicksPerSecond / 2)

// Wandering heading is re-rolled this often. Deliberately not tied to any
// obstacle-avoidance logic -- see travel()'s comment.
#define GHAST_HEADING_TICKS 60

Ghast::Ghast(Level* level)
: Mob(level), chargeTicks(0), attackCooldown(0),
  moveDirX(0), moveDirY(0), moveDirZ(0), moveTicks(0) {
    setSize(4.0f, 4.0f);
    fireImmune = true;
    flying = true;
    health = getMaxHealth();
    entityRendererId = ER_GHAST_RENDERER;
}

int Ghast::getEntityTypeId() const { return EntityTypes::IdGhast; }
int Ghast::getCreatureBaseType() const { return EntityTypes::BaseEnemy; }

bool Ghast::canSpawn() {
    // Real ghast spawn rule: a solid block below and a clear 5x5x4 space.
    // This is a genuinely different shape check from every other mob's
    // spawn validation in this codebase (strider wants a lava surface,
    // ground mobs want a single standable column) since a ghast spawns
    // floating in open air, not standing on anything -- the "solid block
    // below" requirement is about the COLUMN under the space being real
    // ground somewhere below it, not something the ghast itself touches.
    int bx = (int)floorf(x), by = (int)floorf(y), bz = (int)floorf(z);
    for (int dx = -2; dx <= 2; dx++) {
        for (int dz = -2; dz <= 2; dz++) {
            for (int dy = 0; dy < 4; dy++) {
                if (level->isSolidBlockingTile(bx + dx, by + dy, bz + dz)) return false;
            }
        }
    }
    return true;
}

bool Ghast::hurt(Entity* source, int damage) {
    return Mob::hurt(source, damage);
}

void Ghast::die(Entity* source) {
    // achvOnMobKilled first, unconditionally: a ghast still counts toward
    // the shared hostile-kill counters (Monster Hunter, Sniper) no matter
    // how it died, matching how every other hostile mob reports its own
    // death here (see e.g. Zombie/Skeleton's own die() overrides).
    achvOnMobKilled(EntityTypes::IdGhast, 0.0f);

    // Return to Sender specifically wants "killed by ITS OWN deflected
    // fireball" -- source is the Fireball entity that caused the fatal
    // explosion damage (worldExplode calls e->hurt(0, ...) for BLOCK-
    // sourced explosions, but Fireball::tick calls hitEntity->hurt(this,
    // 6) for the direct-impact hit and worldExplode's own blast pass
    // separately reports damage with no source; a ghast within its own
    // blast radius is far more likely to die to the direct-impact hurt()
    // call, which does carry the Fireball as source, so checking here is
    // the right place to catch the common case without needing
    // worldExplode itself to thread a source pointer through the blast
    // damage pass).
    if (source && source->isEntityType(EntityTypes::IdFireball)) {
        Fireball* fb = (Fireball*)source;
        if (fb->deflectedByPlayer) achvOnGhastReturnToSender();
    }

    Mob::die(source);
}

void Ghast::aiStep() {
    if (isImmobile()) {
        xxa = yya = yRotA = 0.0f;
        return;
    }

    // Simple wandering flight per the agreed scope: no obstacle avoidance,
    // no vanilla-accurate "keep 16+ blocks from a spotted player" logic --
    // just a heading that re-rolls periodically, matching how Strider's
    // own unridden wander (yRotA nudged every ~30 ticks) is a cheap
    // stand-in for real steering rather than a faithful port.
    if (--moveTicks <= 0) {
        moveTicks = GHAST_HEADING_TICKS / 2 + sharedRandom.nextInt(GHAST_HEADING_TICKS);
        float yaw   = sharedRandom.nextFloat() * 360.0f * (Mth::PI / 180.0f);
        float pitch = (sharedRandom.nextFloat() - 0.5f) * 0.6f; // mild vertical drift
        moveDirX = sinf(yaw) * cosf(pitch);
        moveDirY = sinf(pitch);
        moveDirZ = cosf(yaw) * cosf(pitch);
    }

    // Bump away from anything solid immediately ahead rather than truly
    // avoiding it -- cheap collision recovery, not pathing. Without this
    // a ghast that wandered into a wall would just push against it
    // forever with no visible reaction, which reads as broken rather than
    // as "not doing real obstacle avoidance."
    int px = (int)floorf(x + moveDirX * 2.0f);
    int py = (int)floorf(y + moveDirY * 2.0f);
    int pz = (int)floorf(z + moveDirZ * 2.0f);
    if (level->isSolidBlockingTile(px, py, pz)) {
        moveDirX = -moveDirX; moveDirY = -moveDirY; moveDirZ = -moveDirZ;
        moveTicks = GHAST_HEADING_TICKS / 2;
    }

    travel(moveDirX, moveDirZ); // yya unused by Ghast::travel; see below
}

void Ghast::travel(float xs, float /*yf*/) {
    // Ghast::aiStep passes its 3D heading through xs (x component) and
    // moveDirZ directly below, rather than through Mob::travel's normal
    // xs/yf (strafe/forward relative to yRot) convention, because a
    // ghast's movement is a free 3D direction, not a 2D ground-relative
    // one -- there's no yRot-based reprojection here the way
    // mobMoveRelative does for walking mobs. xs is accepted only to match
    // the virtual signature; the real heading is read from the member
    // fields directly.
    (void)xs;
    xd = xd * 0.91f + moveDirX * flyingSpeed * 0.4f;
    yd = yd * 0.91f + moveDirY * flyingSpeed * 0.4f;
    zd = zd * 0.91f + moveDirZ * flyingSpeed * 0.4f;

    // No gravity at all -- flying = true is set in the constructor for
    // anything elsewhere that checks it, but Mob's own gravity is applied
    // in Entity::move/Mob's base travel, which this override replaces
    // entirely rather than calling into.
    move(xd, yd, zd);

    yRot = atan2f(xd, zd) * (180.0f / Mth::PI);
}

void Ghast::tick() {
    Mob::tick();

    if (isImmobile()) return;

    if (chargeTicks > 0) {
        chargeTicks--;
        if (chargeTicks == 0 && level->player && level->player->isAlive()) {
            // Spawn point offset toward the player so the fireball starts
            // outside the ghast's own 4x4x4 hitbox -- Fireball::tick's
            // owner exclusion means the ghast itself is never a valid hit
            // target for its own shot anyway, but starting the shot
            // outside the body avoids an ugly one-tick overlap in the
            // renderer before the fireball's first move.
            float dx = level->player->x - x;
            float dy = (level->player->y - level->player->heightOffset + 1.0f) - y;
            float dz = level->player->z - z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist > 1e-3f) { dx /= dist; dy /= dist; dz /= dist; }

            Fireball* fb = new Fireball(level, this, x + dx * 2.5f, y + dy * 2.5f, z + dz * 2.5f,
                                         dx, dy, dz, 0.6f);
            level->addEntity(fb);
            level->playSound(this, "mob.ghast.fireball", getSoundVolume(), 1.0f);
        }
        return; // no new shot can start while mid-charge
    }

    if (attackCooldown > 0) { attackCooldown--; return; }

    if (!level->player || !level->player->isAlive()) return;
    float ddx = x - level->player->x, ddy = y - level->player->y, ddz = z - level->player->z;
    if (ddx * ddx + ddy * ddy + ddz * ddz > mobAiRange() * mobAiRange()) return;
    if (!canSee(level->player)) return;

    chargeTicks = GHAST_CHARGE_TICKS;
    attackCooldown = GHAST_ATTACK_COOLDOWN;
}

int Ghast::getDeathLoot() { return ITEM_GUNPOWDER; }

void Ghast::addAdditonalSaveData(CompoundTag* tag) {
    Mob::addAdditonalSaveData(tag);
}

void Ghast::readAdditionalSaveData(CompoundTag* tag) {
    Mob::readAdditionalSaveData(tag);
}

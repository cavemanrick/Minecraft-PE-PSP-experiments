
#ifndef MCPSP_WORLD_ENTITY_RENDERER_ID_H
#define MCPSP_WORLD_ENTITY_RENDERER_ID_H

enum EntityRendererId {
    ER_DEFAULT_RENDERER,
    ER_QUERY_RENDERER,
    ER_TNT_RENDERER,
    ER_HUMANOID_RENDERER,
    ER_ITEM_RENDERER,
    ER_TRIPODCAMERA_RENDERER,
    ER_CHICKEN_RENDERER,
    ER_COW_RENDERER,
    ER_PIG_RENDERER,
    ER_SHEEP_RENDERER,
    ER_SHEEP_FUR_RENDERER,
    ER_ZOMBIE_RENDERER,
    ER_SKELETON_RENDERER,
    ER_SPIDER_RENDERER,
    ER_CREEPER_RENDERER,
    ER_PIGZOMBIE_RENDERER,
    ER_STRIDER_RENDERER,
    ER_GHAST_RENDERER,
    ER_FIREBALL_RENDERER,
    ER_ARROW_RENDERER,
    ER_PLAYER_RENDERER,
    ER_THROWNEGG_RENDERER,
    ER_SNOWBALL_RENDERER,
    ER_PAINTING_RENDERER,
    ER_VILLAGER_RENDERER,
    ER_WARPED_SPIDER_RENDERER,
    ER_FALLINGTILE_RENDERER,
    ER_FISHING_BOBBER_RENDERER,

    // No RaftRenderer exists yet -- see raft.cpp's constructor comment.
    // This slot is intentionally never assigned in
    // EntityRenderDispatcher's init, so a raft is fully functional
    // (rides, collides, breaks) but invisible until real art/mesh work
    // gives it a renderer to assign here.
    ER_RAFT_RENDERER,

    // Sentinel, not a real renderer id -- always one past the last real
    // entry above. entity_render_dispatcher.h sizes its renderer array off
    // this instead of a named enumerator, so appending a new id here can
    // never again silently shrink that array by one the way
    // ER_FALLINGTILE_RENDERER + 1 (missing ER_FISHING_BOBBER_RENDERER) and
    // then ER_FISHING_BOBBER_RENDERER + 1 (missing ER_RAFT_RENDERER) both
    // did in turn. Must stay the last entry in this enum.
    ER_RENDERER_COUNT
};

#endif

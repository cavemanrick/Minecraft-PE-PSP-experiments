
#ifndef MCPSP_CLIENT_ENTITY_RENDER_DISPATCHER_H
#define MCPSP_CLIENT_ENTITY_RENDER_DISPATCHER_H

#include "world/entity/entity_renderer_id.h"

class Entity;
class EntityRenderer;
class Level;

class EntityRenderDispatcher {
public:
    static EntityRenderDispatcher* getInstance();

    void renderAll(Level* level, float a);

    void render(Entity* entity, float a);

private:
    EntityRenderDispatcher();
    void assign(EntityRendererId id, EntityRenderer* r);
    EntityRenderer* getRenderer(Entity* entity);

    // Was ER_FALLINGTILE_RENDERER + 1, one enumerator short of the actual
    // last entry (ER_FISHING_BOBBER_RENDERER) -- that value's own assign()
    // call in the constructor silently no-op'd (assign() bounds-checks
    // `id < MAX_RENDERERS`), so fishing bobbers likely never rendered.
    //
    // Pinning to a specific named enumerator turned out not to prevent a
    // repeat of exactly this bug -- adding ER_RAFT_RENDERER after
    // ER_FISHING_BOBBER_RENDERER reintroduced the identical off-by-one
    // until this was caught. Using a trailing ER_RENDERER_COUNT sentinel
    // instead: it always sits one past whatever the real last enumerator
    // is, so appending further ids after it can't silently shrink this
    // array again.
    static const int MAX_RENDERERS = ER_RENDERER_COUNT;
    EntityRenderer* _renderers[MAX_RENDERERS];
};

#endif

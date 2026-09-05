
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
    // Pinned to the real last enumerator instead of a specific named one,
    // so adding a new EntityRendererId after this point can't quietly
    // reintroduce the same off-by-one.
    static const int MAX_RENDERERS = ER_FISHING_BOBBER_RENDERER + 1;
    EntityRenderer* _renderers[MAX_RENDERERS];
};

#endif

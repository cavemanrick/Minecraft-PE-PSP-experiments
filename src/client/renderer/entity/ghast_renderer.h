#ifndef MCPSP_CLIENT_RENDERER_ENTITY_GHAST_RENDERER_H
#define MCPSP_CLIENT_RENDERER_ENTITY_GHAST_RENDERER_H

#include "client/renderer/entity/entity_renderer.h"

class GhastRenderer : public EntityRenderer {
public:
    GhastRenderer();
    virtual void render(Entity* e, float x, float y, float z, float rot, float a);
};

#endif

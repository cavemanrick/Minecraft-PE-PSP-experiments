#ifndef MCPSP_CLIENT_RENDERER_ENTITY_STRIDER_RENDERER_H
#define MCPSP_CLIENT_RENDERER_ENTITY_STRIDER_RENDERER_H

#include "client/renderer/entity/entity_renderer.h"

class StriderRenderer : public EntityRenderer {
public:
    StriderRenderer();
    virtual void render(Entity* e, float x, float y, float z, float rot, float a);
};

#endif

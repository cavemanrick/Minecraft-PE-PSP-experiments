#ifndef MCPSP_CLIENT_ENTITY_WARPED_SPIDER_RENDERER_H
#define MCPSP_CLIENT_ENTITY_WARPED_SPIDER_RENDERER_H

#include "client/renderer/entity/entity_renderer.h"
#include "gpu/texture.h"

// Identical model/geometry/animation to SpiderRenderer -- only the texture
// differs (data/images/mob/spider_warped.png, a direct recolor of the
// ordinary spider skin: blue eyes in place of red, plus glowing blue
// streaks on the body). Duplicated rather than shared with SpiderRenderer
// because every other mob renderer in this codebase already follows a
// one-file-per-mob, fully self-contained pattern (see ghast_renderer.cpp,
// creeper_renderer.cpp) with no shared geometry-builder abstraction across
// mob types -- introducing one just for this pair would be inconsistent
// with the rest of the renderer code and risks touching the working
// ordinary-spider renderer for a purely additive request.
class WarpedSpiderRenderer : public EntityRenderer {
public:
    WarpedSpiderRenderer();
    virtual void render(Entity* entity, float x, float y, float z, float rot, float a);

private:
    Texture tex;
    bool    haveTex;
};

#endif

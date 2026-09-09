
#ifndef MCPSP_CLIENT_RAFT_RENDERER_H
#define MCPSP_CLIENT_RAFT_RENDERER_H

#include "client/renderer/entity/entity_renderer.h"

// Draws the raft as a thin, yaw-rotated box textured straight off
// terrain.png's Bamboo Planks cell -- no dedicated raft skin sheet exists
// (unlike Pig/Strider's data/images/mob/*.png), so this reuses the block
// texture that's already loaded for world rendering, the same way
// FallingTileRenderer and PrimedTntRenderer draw entity geometry off
// g_terrain rather than a separate sheet.
class RaftRenderer : public EntityRenderer {
public:
    RaftRenderer() { shadowRadius = 0.6f; shadowStrength = 1.0f; }
    virtual void render(Entity* entity, float x, float y, float z, float rot, float a);
};

#endif

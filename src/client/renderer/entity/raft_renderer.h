
#ifndef MCPSP_CLIENT_RAFT_RENDERER_H
#define MCPSP_CLIENT_RAFT_RENDERER_H

#include "client/renderer/entity/entity_renderer.h"

// Built the same way Pig/Strider are (mobBuildBox + mobRenderParts against
// a dedicated skin sheet), not the old flat-plank-box-off-terrain.png
// approach this replaces. Deck (P_DECK) plus two bow/stern rail posts
// (P_RAIL0/1) -- a simplified stand-in for vanilla's actual 5-piece
// bottom/front/back/left/right boat model, whose real UV islands on this
// same texture are more detailed (per-side crossbars, paddle mounts) than
// this raft's silhouette needs.
class RaftRenderer : public EntityRenderer {
public:
    RaftRenderer() { shadowRadius = 0.6f; shadowStrength = 1.0f; }
    virtual void render(Entity* entity, float x, float y, float z, float rot, float a);
};

#endif

#ifndef MCPSP_CLIENT_FISHING_BOBBER_RENDERER_H
#define MCPSP_CLIENT_FISHING_BOBBER_RENDERER_H

#include "client/renderer/entity/entity_renderer.h"

// Billboarded sprite for the fishing bobber.
//
// This is deliberately a near-copy of ThrowableRenderer rather than a reuse
// of it: that renderer casts its Entity* straight to Throwable* to read
// itemId, and FishingBobber is not a Throwable, so pointing
// ER_FISHING_BOBBER_RENDERER at it would be an invalid cast reading garbage
// as an item id. The duplication is the honest cost of not restructuring
// ThrowableRenderer's interface for one caller.
class FishingBobberRenderer : public EntityRenderer {
public:
    virtual void render(Entity* entity, float x, float y, float z, float rot, float a);
};

#endif

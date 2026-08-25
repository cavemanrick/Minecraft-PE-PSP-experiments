
#ifndef MCPSP_CLIENT_RENDER_H
#define MCPSP_CLIENT_RENDER_H

#include "client/gui/screens/menu.h"

#define SKY_COLOR 0xFFE0A860u

extern unsigned int g_skyColorNow;

extern float g_camX, g_camY, g_camZ;

extern float g_nearZPlane;

void gameRender(MenuState& s);

// Clear colour for the frame about to be drawn. Prefer this over reading
// g_skyColorNow directly at clear time -- see the comment on its definition
// in render.cpp for why the two differ on a dimension change.
unsigned int gameClearColor(void);

bool gameProgressScreenUp();

#endif

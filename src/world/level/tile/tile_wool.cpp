#include "world/level/tile/tiles.h"

// All 16 wool colours share ONE atlas cell.
//
// The 16 wool cells were never independently drawn -- they are the same
// noise pattern in 16 colours. Correlating each cell's per-pixel luminance
// against white wool's gives r >= 0.994 for every colour, so collapsing
// them to a mask plus a tint recovers how the art was made rather than
// approximating it.
//
// The mask is white wool's original cell (0,4), used unchanged: it is
// exactly achromatic (R==G==B on every pixel), fully opaque, and its
// brightest texel is already 255, so white wool with a white tint
// reproduces the original bit for bit.
//
// This frees the other 15 cells:
//   (1,7) (1,8) (1,9) (1,10) (1,11) (1,12) (1,13) (1,14)
//   (2,7) (2,8) (2,9) (2,10) (2,11) (2,12) (2,13)
//
// Note this changes NOTHING about how wool is stored or identified. Wool
// was always a single block id (BLOCK_WOOL = 35) with the colour in the
// data nibble; only the texture lookup changes. Saves, recipes, drops and
// the dye system are untouched.
#define WOOL_MASK_COL 0
#define WOOL_MASK_ROW 4

// Least-squares fit of each original cell against the mask, clamped to the
// representable 0..255 range. Mean absolute error across all 16 colours is
// 2.82/255; the worst is orange at 8.74, whose ideal red component wants
// 267 and has to clamp. Invisible on the PSP panel.
//
// Packed 0xAABBGGRR to match how tileForBlock's tint is unpacked
// (r = tint & 0xFF, g = >>8, b = >>16, a = >>24) -- see hud.cpp and
// particle.cpp. Getting this byte order backwards swaps red and blue.
static const unsigned int kWoolTint[16] = {
    0xFFFFFFFFu, //  0 white        rgb(255,255,255)
    0xFF4092FFu, //  1 orange       rgb(255,146,64)
    0xFFE658DAu, //  2 magenta      rgb(218,88,230)
    0xFFF2A079u, //  3 light blue   rgb(121,160,242)
    0xFF21D0DFu, //  4 yellow       rgb(223,208,33)
    0xFF37D944u, //  5 lime         rgb(68,217,55)
    0xFFB298F9u, //  6 pink         rgb(249,152,178)
    0xFF4C4C4Cu, //  7 gray         rgb(76,76,76)
    0xFFBEBEB6u, //  8 light gray   rgb(182,190,190)
    0xFFAC862Du, //  9 cyan         rgb(45,134,172)
    0xFFE03E95u, // 10 purple       rgb(149,62,224)
    0xFFB13B2Du, // 11 blue         rgb(45,59,177)
    0xFF203B63u, // 12 brown        rgb(99,59,32)
    0xFF1C5940u, // 13 green        rgb(64,89,28)
    0xFF2F33BCu, // 14 red          rgb(188,51,47)
    0xFF1B1B1Fu, // 15 black        rgb(31,27,27)
};

void tileWool(unsigned char data, int f, int* col, int* row, unsigned int* tint) {
    (void)f; // every face of a wool block is identical
    *col = WOOL_MASK_COL;
    *row = WOOL_MASK_ROW;
    if (tint) *tint = kWoolTint[data & 0xF];
}

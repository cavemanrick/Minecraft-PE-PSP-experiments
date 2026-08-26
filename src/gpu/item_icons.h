
#ifndef MCPSP_GPU_ITEM_ICONS_H
#define MCPSP_GPU_ITEM_ICONS_H

#define II_BOOTS_CHAIN        112
#define II_BOW_PULL_0         115
#define II_BOW_PULL_1         116
#define II_BOW_PULL_2         117
#define II_BUCKET_EMPTY       121
#define II_BUCKET_LAVA        123
#define II_BUCKET_MILK        124
#define II_BUCKET_WATER       122
#define II_CAKE               120
#define II_CAMERA             114
#define II_CHESTPLATE_CHAIN   110
#define II_EGG                113
#define II_HELMET_CHAIN       109
#define II_LEGGINGS_CHAIN     111
#define II_SPAWN_EGG_BASE     118
#define II_SPAWN_EGG_OVERLAY  119

// Index 19 holds the fishing rod's art. It did NOT always: 19 was the
// bow's idle icon, and the rod was painted over that cell. The bow's table
// entry below was never moved off 19, so the two shared a sprite and a bow
// in hand or in the hotbar looked exactly like a fishing rod -- which also
// made the real rod look unobtainable, since the slot beside it in the
// creative palette was an identical-looking bow.
//
// Real idle-bow art, vanilla items.png (5,1), pasted into free slot 125.
// 19 is the fishing rod and was ALSO the bow's entry in the table below --
// the rod had been painted over the bow's old idle cell without the bow
// being moved off it, so a bow rendered as a rod and the real rod beside
// it in the creative palette looked like a duplicate.
#define II_BOW_IDLE                 125
#define II_FISHING_ROD               19

// Real fish art, vanilla items.png (9,5) and (10,5). These pointed at the
// raw/cooked CHICKEN cells (34/35) as a stopgap, so fish and chicken were
// indistinguishable in the inventory.
#define II_FISH_RAW                 126
#define II_FISH_COOKED              127

// Dark oak leaves, taken from terrain.png (14,1) -- already dark-tinted
// there, so it needs no recolouring for the GUI.
//
// Referenced from hud.cpp as 128 + this value, NOT as this value: the
// block-icon path treats indices under 128 as 48x48 isometric cubes and
// anything at or above 128 as (index - 128) into this same 16x16 flat
// grid. The other leaf variants are real isometric cubes at 83/84/85; a
// flat sprite is the cheap way in without drawing a cube.
#define II_LEAVES_DARK_OAK          128

// Remaining free flat slots: 129, 139-141, 147-159, plus 0,6,7,9,107,108.
// Same convention throughout this atlas -- 16x16 at
// column = index & 31, row = 27 + (index >> 5), binary (0/255) alpha, no
// partial edge pixels, because the loader is GU_PSM_5551 (1-bit alpha).

static const short kItemIcon[256] = {
       46,    45,    47,   103,   106, II_BOW_IDLE,    62,    -1,   // 261 = ITEM_BOW at index 5
       25,    22,    21,    18,    57,    38,    36,    39,
       58,    42,    41,    43,    59,    49,    48,    50,
       37,    67,    66,    60,    53,    52,    54,    61,
       63,   100,    40,    44,    17,    51,    55,    14,
      105,   104,    70,    72,    73,    74,   109,   110,
      111,   112,    75,    76,    77,    78,    79,    80,
       81,    82,    83,    84,    85,    86,    64,    28,
       29,     5,    -1,    20,     3,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    98,    -1,    71,    -1,
       31,    30,    10,    96,    97,    -1,    -1,    -1,
      113,    -1, II_FISHING_ROD,    -1,    99, II_FISH_RAW,            II_FISH_COOKED,    -1,
       95,   101,   120,     4,    -1,    -1,    -1,    56,
       68,    -1,    15,    32,    33,    34,    35,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    27,    69,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      114,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
};

static const short kItemIconCoal[16] = { 102, 23, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static const short kItemIconDye[16] = { -1, 24, 26, 146, 65, 87, 88, -1, -1, 89, 90, 91, 92, 93, 94, 16 };

#endif

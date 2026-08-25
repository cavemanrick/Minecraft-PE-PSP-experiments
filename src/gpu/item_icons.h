
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

// Fishing rod: real art, at index 19 (an unused violet placeholder slot in
// gui_blocks.png before this). The two fish icons are still a stopgap: no
// dedicated raw/cooked fish sprite exists yet, so they point at the
// raw/cooked chicken art (indices 34/35) -- fish and chicken currently
// look identical in the inventory.
//
// Free sprite slots exist (125-147 and 0,1,2,6,7,8,9,11,12,13) for
// whenever real fish art is drawn in. column = index & 31,
// row = 27 + (index >> 5), same binary (0/255) alpha convention this
// atlas already uses throughout.
#define II_FISHING_ROD               19
#define II_FISH_RAW_PLACEHOLDER      34
#define II_FISH_COOKED_PLACEHOLDER   35

// Real fishing rod art now lives at index 19 -- see the comment there. The
// two fish icons are still a stopgap: no dedicated raw/cooked fish sprite
// exists in gui_blocks.png yet, so they point at the raw/cooked chicken
// art (indices 34/35), meaning fish and chicken currently look identical
// in the inventory.
//
// Free sprite slots do exist (125-147 and 0,1,2,6,7,8,9,11,12,13) for
// whenever real fish art is drawn in. Placing it is the same recipe used
// for the rod: paint a 16x16 sprite into gui_blocks.png at
// column = index & 31, row = 27 + (index >> 5), with the exact convention
// this atlas already uses -- binary (0/255) alpha, no partial edge pixels
// -- then repoint just these two defines. Nothing else references them.
//
// Index 19 was the bow's idle icon before the rod was drawn in (see
// II_BOW_PULL_0/1/2 a few lines up for the bow's OTHER three icons, which
// are unrelated cells and still the bow -- only the single idle-hold icon
// was ever shared with the rod placeholder).
#define II_FISHING_ROD               19
#define II_FISH_RAW_PLACEHOLDER      34
#define II_FISH_COOKED_PLACEHOLDER   35

static const short kItemIcon[256] = {
       46,    45,    47,   103,   106,    19,    62,    -1,
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
      113,    -1, II_FISHING_ROD,    -1,    99, II_FISH_RAW_PLACEHOLDER, II_FISH_COOKED_PLACEHOLDER,    -1,
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

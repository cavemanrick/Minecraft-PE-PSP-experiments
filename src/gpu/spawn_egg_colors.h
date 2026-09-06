
#ifndef MCPSP_GPU_SPAWN_EGG_COLORS_H
#define MCPSP_GPU_SPAWN_EGG_COLORS_H

static inline unsigned int eggAbgr(unsigned int rgb) {
    unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

static inline bool spawnEggColors(short data, unsigned int* base, unsigned int* spot) {
    unsigned int c1, c2;
    switch (data) {
        case 10: c1 = 0xa1a1a1; c2 = 0xff0000; break;
        case 11: c1 = 0x443626; c2 = 0xa1a1a1; break;
        case 12: c1 = 0xf0a5a2; c2 = 0xdb635f; break;
        case 13: c1 = 0xe7e7e7; c2 = 0xffb5b5; break;
        case 32: c1 = 0x00afaf; c2 = 0x799c65; break;
        case 33: c1 = 0x0da70b; c2 = 0x000000; break;
        case 34: c1 = 0xc1c1c1; c2 = 0x494949; break;
        case 35: c1 = 0x342d27; c2 = 0xa80e0e; break;
        case 36: c1 = 0xea9393; c2 = 0x4c7129; break;
        case 37: c1 = 0x9c3436; c2 = 0x4d494d; break; // strider (EntityTypes::IdStrider) -- real vanilla values
        case 38: c1 = 0xf9f9f9; c2 = 0xbcbcbc; break; // ghast          -- real vanilla values
        case 39: c1 = 0x563c33; c2 = 0xbd8b72; break; // villager       -- real vanilla values
        // Warped spider is an MCPSP mob with no vanilla egg to copy. Cave
        // spider's silhouette colours (0x0c424e / 0x7a1919) recoloured
        // towards the warped palette its texture uses, so the egg reads as
        // "spider, but warped" beside the plain spider's egg at data 35
        // rather than as a second identical-looking spider egg.
        case 40: c1 = 0x0c4e4a; c2 = 0x1a7b7b; break; // warped spider
        default: *base = *spot = 0xFFFFFFFFu; return false;
    }
    *base = eggAbgr(c1);
    *spot = eggAbgr(c2);
    return true;
}

static inline unsigned int eggMul(unsigned int a, unsigned int b) {
    unsigned int r = ((a & 0xFF) * (b & 0xFF)) / 255;
    unsigned int g = (((a >> 8) & 0xFF) * ((b >> 8) & 0xFF)) / 255;
    unsigned int bl = (((a >> 16) & 0xFF) * ((b >> 16) & 0xFF)) / 255;
    unsigned int al = (((a >> 24) & 0xFF) * ((b >> 24) & 0xFF)) / 255;
    return (al << 24) | (bl << 16) | (g << 8) | r;
}

#endif

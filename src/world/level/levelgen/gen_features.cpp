#include "world/level/levelgen/gen_features.h"

const GenFeatureDef kGenFeatures[GEN_FEATURE_COUNT] = {

    { "Caves", true },
    { "Villages", true },
    { "Dungeons", true },
    { "Nether Fortresses", true },
};

int genFeaturesDefaultMask() {
    int mask = 0;
    for (int i = 0; i < GEN_FEATURE_COUNT; i++)
        if (kGenFeatures[i].defaultOn) mask |= 1 << i;
    return mask;
}

bool genFeatureEnabled(int mask, int feature) {
    return (mask & (1 << feature)) != 0;
}

int genFeatureToggled(int mask, int feature) {
    return mask ^ (1 << feature);
}

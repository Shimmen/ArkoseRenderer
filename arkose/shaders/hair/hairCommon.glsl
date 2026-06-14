#ifndef HAIR_COMMON_GLSL
#define HAIR_COMMON_GLSL

struct HairInterpolants {
    uint strandIndex;
};

struct HairConstants {
    bool withMaterialColor;
    uint drawableIdx;
    uint firstStrand;
    uint strandCount;
};

// DOM = Deep Opacity Map
#define HAIR_DOM_LAYER_COUNT 4
#define HAIR_DOM_LAYER_THICKNESS 0.02

#endif // HAIR_COMMON_GLSL

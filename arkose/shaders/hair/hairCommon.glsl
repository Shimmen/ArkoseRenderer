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

#endif // HAIR_COMMON_GLSL

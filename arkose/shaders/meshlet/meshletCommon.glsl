#ifndef MESHLET_COMMON_GLSL
#define MESHLET_COMMON_GLSL

struct MeshShaderInterpolants {
    uint drawableIdx;

    // TODO: Use uint8 for optimal packing!
    //uint baseID; uint8_t subIDs[GROUP_SIZE];
    uint meshletIndices[GROUP_SIZE];
};

#endif // MESHLET_COMMON_GLSL

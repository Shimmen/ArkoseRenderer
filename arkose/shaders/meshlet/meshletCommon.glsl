#ifndef MESHLET_COMMON_GLSL
#define MESHLET_COMMON_GLSL

#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

struct MeshShaderInterpolants {
    uint drawableIdx;

    uint meshletBaseIndex;
    uint8_t meshletRelativeIndices[GROUP_SIZE];
};

#endif // MESHLET_COMMON_GLSL

#ifndef MESHLET_COMMON_GLSL
#define MESHLET_COMMON_GLSL

#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

// TODO: This doesn't work in HLSL!
#define meshlet_rel_idx_t uint8_t

struct MeshShaderInterpolants {
    uint drawableIdx;

    uint meshletBaseIndex;
    meshlet_rel_idx_t meshletRelativeIndices[GROUP_SIZE];
};

#endif // MESHLET_COMMON_GLSL

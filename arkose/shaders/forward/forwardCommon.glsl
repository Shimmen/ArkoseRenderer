#ifndef FORWARD_COMMON_GLSL
#define FORWARD_COMMON_GLSL

#ifndef FORWARD_MESH_SHADING
#define FORWARD_MESH_SHADING 0
#endif

struct ForwardPassConstants {
    float ambientAmount;
    vec2 frustumJitterCorrection;
    vec2 invTargetSize;

#if FORWARD_MESH_SHADING
    bool frustumCullMeshlets;
#endif
};

#endif // FORWARD_COMMON_GLSL

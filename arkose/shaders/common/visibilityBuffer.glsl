#ifndef VISIBILITY_BUFFER_GLSL
#define VISIBILITY_BUFFER_GLSL

#extension GL_EXT_scalar_block_layout : require

#include <common/geometry.glsl>

// Corresponding to published binding set "VisibilityBufferData"
#define DeclareCommonBindingSet_VisibilityBuffer(index)                                                         \
    layout(set = index, binding = 0) uniform usampler2D _visbufDrawableVisibilityTex;                           \
    layout(set = index, binding = 1) uniform usampler2D _visbufTriangleVisibilityTex;

#define visbuf_fetchDrawableIdx(pixelCoord) uint(texelFetch(_visbufDrawableVisibilityTex, pixelCoord, 0).x)
#define visbuf_fetchTriangleId(pixelCoord) uint(texelFetch(_visbufTriangleVisibilityTex, pixelCoord, 0).x)

#define visbuf_isValidDrawableIdx(drawableIdx) (drawableIdx > 0)
#define visbuf_getInstanceFromDrawableIdx(drawableIdx) geom_getDrawable(drawableIdx - 1)

// NOTE: DeclareCommonBindingSet_Geometry must be declared before this
#define DeclareVisibilityBufferSamplingFunctions                                                  \
                                                                                                  \
    uvec3 visbuf_calculateTriangle(ivec2 pixelCoord)                                              \
    {                                                                                             \
        uint triangleId = visbuf_fetchTriangleId(pixelCoord);                                     \
        uint meshletTriangleIdx = triangleId & 0xff; /* todo! */                                  \
        uint meshletIdx = (triangleId >> 8) - 1; /* todo! */                                      \
                                                                                                  \
        ShaderMeshlet meshlet = geom_getMeshlet(meshletIdx);                                       \
        uint meshletBaseIdxLookup = meshlet.firstIndex + (3 * meshletTriangleIdx);                \
        uint i0 = geom_getMeshletVertexIndirection(geom_getMeshletIndex(meshletBaseIdxLookup + 0)); \
        uint i1 = geom_getMeshletVertexIndirection(geom_getMeshletIndex(meshletBaseIdxLookup + 1)); \
        uint i2 = geom_getMeshletVertexIndirection(geom_getMeshletIndex(meshletBaseIdxLookup + 2)); \
                                                                                                  \
        return uvec3(i0, i1, i2);                                                                 \
    }                                                                                             \
                                                                                                  \
    void visbuf_getTriangleVertexPositions(uvec3 triangle, out vec3 p0,                           \
                                                           out vec3 p1,                           \
                                                           out vec3 p2)                           \
    {                                                                                             \
        p0 = geom_getMeshletPosition(triangle[0]);                                                \
        p1 = geom_getMeshletPosition(triangle[1]);                                                \
        p2 = geom_getMeshletPosition(triangle[2]);                                                \
    }                                                                                             \
                                                                                                  \
    void visbuf_getTriangleVertexAttributes(uvec3 triangle, out VertexAttributes v0,              \
                                                            out VertexAttributes v1,              \
                                                            out VertexAttributes v2)              \
    {                                                                                             \
        v0 = geom_getMeshletVertexAttributes(triangle[0]);                                        \
        v1 = geom_getMeshletVertexAttributes(triangle[1]);                                        \
        v2 = geom_getMeshletVertexAttributes(triangle[2]);                                        \
    }

#endif // VISIBILITY_BUFFER_GLSL

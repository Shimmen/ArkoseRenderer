#ifndef VISIBILITY_BUFFER_GLSL
#define VISIBILITY_BUFFER_GLSL

#extension GL_EXT_scalar_block_layout : require

// Corresponding to published binding set "VisibilityBufferData"
#define DeclareCommonBindingSet_VisibilityBuffer(index)                                                                                         \
    layout(set = index, binding = 0)         uniform usampler2D _visbufDrawableVisibilityTex;                                                   \
    layout(set = index, binding = 1)         uniform usampler2D _visbufTriangleVisibilityTex;                                                   \
    layout(set = index, binding = 2)         buffer restrict readonly VisbufInstanceBlock     { ShaderDrawable    _visbufInstances[]; };        \
    layout(set = index, binding = 3)         buffer restrict readonly VisbufMeshletBlock      { ShaderMeshlet     _visbufMeshlets[]; };         \
    layout(set = index, binding = 4, scalar) buffer restrict readonly VisbufIndicesBlock      { uint              _visbufMeshletIndices[]; };   \
    layout(set = index, binding = 5, scalar) buffer restrict readonly VisbufPositionsBlock    { vec3              _visbufMeshletPositions[]; }; \
    layout(set = index, binding = 6, scalar) buffer restrict readonly VisbufNonPositionsBlock { NonPositionVertex _visbufMeshletVertices[]; };

#define visbuf_fetchDrawableIdx(pixelCoord) uint(texelFetch(_visbufDrawableVisibilityTex, pixelCoord, 0).x)
#define visbuf_fetchTriangleId(pixelCoord) uint(texelFetch(_visbufTriangleVisibilityTex, pixelCoord, 0).x)

#define visbuf_isValidDrawableIdx(drawableIdx) (drawableIdx > 0)
#define visbuf_getInstanceFromDrawableIdx(drawableIdx) _visbufInstances[drawableIdx - 1]

// NOTE: The visibility buffer binding set needs to be declared beclared before this
#define DeclareVisibilityBufferSamplingFunctions                                                  \
                                                                                                  \
    uvec3 visbuf_calculateTriangle(ivec2 pixelCoord)                                              \
    {                                                                                             \
        uint triangleId = visbuf_fetchTriangleId(pixelCoord);                                     \
        uint meshletTriangleIdx = triangleId & 0xff; /* todo! */                                  \
        uint meshletIdx = (triangleId >> 8) - 1; /* todo! */                                      \
                                                                                                  \
        ShaderMeshlet meshlet = _visbufMeshlets[meshletIdx];                                      \
        uint meshletBaseIdxLookup = meshlet.firstIndex + (3 * meshletTriangleIdx);                \
        uint i0 = _visbufMeshletIndices[meshletBaseIdxLookup + 0];                                \
        uint i1 = _visbufMeshletIndices[meshletBaseIdxLookup + 1];                                \
        uint i2 = _visbufMeshletIndices[meshletBaseIdxLookup + 2];                                \
                                                                                                  \
        return uvec3(i0, i1, i2);                                                                 \
    }                                                                                             \
                                                                                                  \
    void visbuf_getTriangleVertexPositions(uvec3 triangle, out vec3 p0,                           \
                                                           out vec3 p1,                           \
                                                           out vec3 p2)                           \
    {                                                                                             \
        p0 = _visbufMeshletPositions[triangle[0]];                                                \
        p1 = _visbufMeshletPositions[triangle[1]];                                                \
        p2 = _visbufMeshletPositions[triangle[2]];                                                \
    }                                                                                             \
                                                                                                  \
    void visbuf_getTriangleVertexNonPositionData(uvec3 triangle, out NonPositionVertex v0,        \
                                                                 out NonPositionVertex v1,        \
                                                                 out NonPositionVertex v2)        \
    {                                                                                             \
        v0 = _visbufMeshletVertices[triangle[0]];                                                 \
        v1 = _visbufMeshletVertices[triangle[1]];                                                 \
        v2 = _visbufMeshletVertices[triangle[2]];                                                 \
    }

#endif // VISIBILITY_BUFFER_GLSL

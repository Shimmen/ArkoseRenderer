#ifndef GEOMETRY_GLSL
#define GEOMETRY_GLSL

#extension GL_EXT_scalar_block_layout : require

#include <shared/SceneData.h>

// Corresponding to "SceneGeometrySet"
#define DeclareCommonBindingSet_Geometry(index)                                                                                            \
    layout(set = index, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable _shaderDrawables[]; };                                 \
                                                                                                                                            \
    /* Regular mesh data */                                                                                                                 \
    layout(set = index, binding = 1, scalar) buffer readonly IndicesBlock { uint _indices[]; };                                             \
    layout(set = index, binding = 2, scalar) buffer readonly PositionsBlock { vec3 _positions[]; };                                         \
    layout(set = index, binding = 3, scalar) buffer readonly AttributesBlock { VertexAttributes _vertexAttributes[]; };                     \
                                                                                                                                            \
    /* Meshlet data */                                                                                                                      \
    layout(set = index, binding = 4) buffer readonly MeshletBlock { ShaderMeshlet _meshlets[]; };                                           \
    layout(set = index, binding = 5, scalar) buffer readonly MeshletIndexBlock { uint _meshletIndices[]; };                                 \
    layout(set = index, binding = 6, scalar) buffer readonly MeshletVertexIndirectionBlock { uint _meshletVertexIndirection[]; };           \
    layout(set = index, binding = 7, scalar) buffer readonly MeshletPositionsBlock { vec3 _meshletPositions[]; };                           \
    layout(set = index, binding = 8, scalar) buffer readonly MeshletAttributesBlock { VertexAttributes _meshletVertexAttributes[]; };       \
                                                                                                                                            \
    /* Hair geometry */                                                                                                                     \
    layout(set = index, binding = 9)  buffer readonly HairStrandBlock { HairStrand _hairStrands[]; };                                       \
    layout(set = index, binding = 10, scalar) buffer readonly HairPositionBlock { vec3 _hairPositions[]; };                                 \
    layout(set = index, binding = 11, scalar) buffer readonly HairAttributeBlock { uint _hairAttributes[]; };

#define geom_getDrawable(drawableIdx) _shaderDrawables[drawableIdx]

// Regular mesh data
#define geom_getMeshIndex(index) _indices[index]
#define geom_getMeshVertexPosition(index) _positions[index]
#define geom_getMeshVertexAttributes(index) _vertexAttributes[index]

// Meshlet data
#define geom_getMeshlet(meshletIdx) _meshlets[meshletIdx]
#define geom_getMeshletIndex(index) _meshletIndices[index]
#define geom_getMeshletVertexIndirection(index) _meshletVertexIndirection[index]
#define geom_getMeshletPosition(index) _meshletPositions[index]
#define geom_getMeshletVertexAttributes(index) _meshletVertexAttributes[index]

// Hair geometry
#define geom_getHairStrand(strandIdx) _hairStrands[strandIdx]
#define geom_getHairPosition(index) _hairPositions[index]
#define geom_getHairAttribute(index) _hairAttributes[index]

#endif // GEOMETRY_GLSL

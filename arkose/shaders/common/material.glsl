#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

#extension GL_EXT_nonuniform_qualifier : require

#include <shared/SceneData.h>

// Corresponding to `GpuScene::globalMaterialBindingSet()`
#define DeclareCommonBindingSet_Material(index)                                                            \
    layout(set = index, binding = 0) buffer readonly MaterialBlock { ShaderMaterial _shaderMaterials[]; }; \
    layout(set = index, binding = 1) uniform sampler2D _bindlessTextures[];

#define material_getMaterial(materialIdx) _shaderMaterials[materialIdx]
#define material_getTexture(textureIdx) _bindlessTextures[nonuniformEXT(textureIdx)]

#endif // MATERIAL_GLSL

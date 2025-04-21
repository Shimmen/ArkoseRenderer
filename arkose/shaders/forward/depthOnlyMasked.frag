#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include <common/material.glsl>
#include <shared/SceneData.h>

layout(location = 0) flat in int vMaterialIndex;
layout(location = 1) in vec2 vTexCoord;

DeclareCommonBindingSet_Material(1)

void main()
{
    ShaderMaterial material = material_getMaterial(vMaterialIndex);
    float mask = texture(material_getTexture(material.baseColor), vTexCoord).a;
    if (mask < material.maskCutoff) {
        discard;
    }
}

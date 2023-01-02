#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include <shared/SceneData.h>

layout(location = 0) flat in int vMaterialIndex;
layout(location = 1) in vec2 vTexCoord;

layout(set = 1, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 1, binding = 1) uniform sampler2D textures[];

void main()
{
    ShaderMaterial material = materials[vMaterialIndex];
    float mask = texture(textures[nonuniformEXT(material.baseColor)], vTexCoord).a;
    if (mask < material.maskCutoff) {
        discard;
    }
}

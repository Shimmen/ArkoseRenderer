#version 450
#extension GL_ARB_separate_shader_objects : enable

#include <shared/ForwardData.h>

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in mat3 vTbnMatrix;
layout(location = 10) flat in int vMaterialIndex;

layout(binding = 2) uniform MaterialBlock
{
    ForwardMaterial materials[FORWARD_MAX_MATERIALS];
};

layout(binding = 3) uniform sampler2D uSamplers[FORWARD_MAX_TEXTURES];

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;

void main()
{
    ForwardMaterial material = materials[vMaterialIndex];

    vec3 baseColor = texture(uSamplers[material.baseColor], vTexCoord).rgb;

    vec3 packedNormal = texture(uSamplers[material.normalMap], vTexCoord).rgb;
    vec3 mappedNormal = normalize(packedNormal * 2.0 - 1.0);
    vec3 N = normalize(vTbnMatrix * mappedNormal); // TODO: Clean up TBN?

    oColor = vec4(baseColor, 1.0);
    oNormal = vec4(N * 0.5 + 0.5, 0.0);
}

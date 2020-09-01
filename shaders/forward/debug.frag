#version 460

#include <shared/CameraState.h>
#include <shared/SceneData.h>

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) flat in int vMaterialIndex;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(set = 1, binding = 1) uniform MaterialBlock { ShaderMaterial materials[SCENE_MAX_MATERIALS]; };
layout(set = 1, binding = 2) uniform sampler2D textures[SCENE_MAX_TEXTURES];

layout(location = 0) out vec4 oColor;

void main()
{
    ShaderMaterial material = materials[vMaterialIndex];
    vec4 inputBaseColor = texture(textures[material.baseColor], vTexCoord).rgba;
    if (inputBaseColor.a < 1e-2) {
        discard;
        return;
    }

    vec3 baseColor = inputBaseColor.rgb;
    oColor = vec4(baseColor, 1.0);
}

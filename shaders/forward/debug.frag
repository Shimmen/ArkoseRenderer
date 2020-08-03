#version 460
#extension GL_ARB_separate_shader_objects : enable

#include <shared/CameraState.h>
#include <shared/ForwardData.h>
#include <shared/LightData.h>
#include <common/brdf.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vPosition;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in mat3 vTbnMatrix;

layout(set = 0, binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(set = 1, binding = 1) uniform sampler2D uBaseColor;

layout(location = 0) out vec4 oColor;

void main()
{
    vec4 inputBaseColor = texture(uBaseColor, vTexCoord).rgba;
    if (inputBaseColor.a < 1e-2) {
        discard;
    }

    vec3 baseColor = inputBaseColor.rgb;
    oColor = vec4(baseColor, 1.0);
}

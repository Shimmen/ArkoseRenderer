#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <shared/LightData.h>
#include <shared/SceneData.h>
#include <shared/ShadowData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 0, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable perObject[]; };
layout(set = 1, binding = 0) buffer readonly ShadowDataBlock { PerLightShadowData lightDatas[]; };

NAMED_UNIFORMS(pushConstants,
    uint lightIndex;
)

void main()
{
    int objectIndex = gl_InstanceIndex;
    mat4 worldFromLocal = perObject[objectIndex].worldFromLocal;

    PerLightShadowData lightData = lightDatas[pushConstants.lightIndex];

    gl_Position = lightData.lightProjectionFromWorld * worldFromLocal * vec4(aPosition, 1.0);

    // TODOL Add support for shadow pancaking!?

    vec3 worldNormal = normalize((worldFromLocal * vec4(aNormal, 0.0)).xyz);
    vec3 worldLightDir = vec3(lightData.lightViewFromWorld[0].z, lightData.lightViewFromWorld[1].z, lightData.lightViewFromWorld[2].z);

    float LdotN = abs(dot(worldLightDir, worldNormal));
    float slope = (LdotN > 0.0) ? sqrt(saturate(1.0 - square(LdotN))) / LdotN : 0.0;

    float depthBias = lightData.constantBias + (slope * lightData.slopeBias);// * lightData.constantBias);

    gl_Position.z += depthBias;
}

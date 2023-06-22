#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;

layout(set = 0, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable perObject[]; };

NAMED_UNIFORMS(constants,
    mat4 lightProjectionFromWorld;
    vec3 worldLightDirection;
    float constantBias;
    float slopeBias;
)

void main()
{
    int objectIndex = gl_InstanceIndex;
    mat4 worldFromLocal = perObject[objectIndex].worldFromLocal;

    gl_Position = constants.lightProjectionFromWorld * worldFromLocal * vec4(aPosition, 1.0);

    // TODOL Add support for shadow pancaking!?

    vec3 worldNormal = normalize((worldFromLocal * vec4(aNormal, 0.0)).xyz);
    // TODO: Use the actual light direction here (frag_pos - light_pos) so we can support point lights too
    float LdotN = abs(dot(constants.worldLightDirection, worldNormal));
    float slope = (LdotN > 0.0) ? sqrt(saturate(1.0 - square(LdotN))) / LdotN : 0.0;

    float depthBias = constants.constantBias + (slope * constants.slopeBias);// * lightData.constantBias);

    gl_Position.z += depthBias;
}

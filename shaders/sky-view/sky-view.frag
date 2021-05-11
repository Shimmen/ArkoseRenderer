#version 460

#include <common/namedUniforms.glsl>
#include <common/spherical.glsl>

layout(location = 0) in vec3 vViewRay;

layout(set = 0, binding = 1) uniform sampler2D environmentTex;

NAMED_UNIFORMS(pushConstants,
    float environmentMultiplier;
)

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 sampleUv = sphericalUvFromDirection(normalize(vViewRay));
    vec3 environment = texture(environmentTex, sampleUv).rgb;
    environment *= pushConstants.environmentMultiplier;
    oColor = vec4(environment, 1.0);
}

#version 460

#include <common/namedUniforms.glsl>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D finalTexture;
layout(set = 0, binding = 1) uniform sampler2DArray filmGrainTexture;

NAMED_UNIFORMS(pushConstants,
    float filmGrainGain;
    float filmGrainScale;
    uint filmGrainArrayIdx;
)

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 finalPixel = texture(finalTexture, vTexCoord).rgb;

    // TODO: Maybe not strictly accurate, but I think it makes sense to add more film grain at higher ISO values
    // and lower scene energy values. I.e. the lower signal-to-noise ratio at the sensor the more noise/"film grain".
    vec2 filmGrainUv = gl_FragCoord.xy / (vec2(textureSize(filmGrainTexture, 0).xy) * pushConstants.filmGrainScale);
    vec3 lookupCoord = vec3(filmGrainUv, float(pushConstants.filmGrainArrayIdx));
    vec3 filmGrain01 = textureLod(filmGrainTexture, lookupCoord, 0.0).rgb;
    vec3 filmGrain = vec3(pushConstants.filmGrainGain * (2.0 * filmGrain01 - 1.0));

    vec3 finalColor = clamp(finalPixel + filmGrain, vec3(0.0), vec3(1.0));
    oColor = vec4(finalColor, 1.0);
}

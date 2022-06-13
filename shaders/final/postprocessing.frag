#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D finalTexture;
layout(set = 0, binding = 1) uniform sampler2DArray filmGrainTexture;

NAMED_UNIFORMS(constants,
    float filmGrainGain;
    float filmGrainScale;
    uint filmGrainArrayIdx;

    float vignetteIntensity;
    float aspectRatio;
)

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 finalPixel = texture(finalTexture, vTexCoord).rgb;

    // Apply natural vignette (NOTE: this is "natural" vignette, i.e. caused by the angle of incident light striking
    // the sensor. It is a very discrete effect and you shouldn't expect to see a massive difference from it.
    {
        vec2 pixelFromCenter = vTexCoord - vec2(0.5);
        pixelFromCenter.x *= constants.aspectRatio;
        float dist = length(pixelFromCenter) * constants.vignetteIntensity;
        float falloffFactor = 1.0 / square(square(dist) + 1.0);
        finalPixel.rgb *= falloffFactor;
    }

    // TODO: Maybe not strictly accurate, but I think it makes sense to add more film grain at higher ISO values
    // and lower scene energy values. I.e. the lower signal-to-noise ratio at the sensor the more noise/"film grain".
    vec2 filmGrainUv = gl_FragCoord.xy / (vec2(textureSize(filmGrainTexture, 0).xy) * constants.filmGrainScale);
    vec3 lookupCoord = vec3(filmGrainUv, float(constants.filmGrainArrayIdx));
    vec3 filmGrain01 = textureLod(filmGrainTexture, lookupCoord, 0.0).rgb;
    vec3 filmGrain = vec3(constants.filmGrainGain * (2.0 * filmGrain01 - 1.0));

    vec3 finalColor = clamp(finalPixel + filmGrain, vec3(0.0), vec3(1.0));
    oColor = vec4(finalColor, 1.0);
}

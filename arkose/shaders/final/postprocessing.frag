#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <common/sampling.glsl>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D finalTexture;
layout(set = 0, binding = 1) uniform sampler2DArray filmGrainTexture;
layout(set = 0, binding = 2) uniform sampler3D colorGradingLutTexture;

NAMED_UNIFORMS(constants,
    float filmGrainGain;
    float filmGrainScale;
    uint filmGrainArrayIdx;

    float vignetteIntensity;
    float aspectRatio;

    vec4 blackBarsLimits;

    bool colorGrade;
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

    // Apply film grain
    vec2 filmGrainUv = gl_FragCoord.xy / (vec2(textureSize(filmGrainTexture, 0).xy) * constants.filmGrainScale);
    vec3 lookupCoord = vec3(filmGrainUv, float(constants.filmGrainArrayIdx));
    vec3 filmGrain01 = textureLod(filmGrainTexture, lookupCoord, 0.0).rgb;
    vec3 filmGrain = vec3(constants.filmGrainGain * (2.0 * filmGrain01 - 1.0));
    filmGrain *= pow(1.0 - luminance(finalPixel), 25.0);
    finalPixel = clamp(finalPixel + filmGrain, vec3(0.0), vec3(1.0));

    // Apply color grading
    if (constants.colorGrade) {
        vec3 colorGradeLookupCoord = clamp(finalPixel, vec3(0.0), vec3(1.0));

        // Tetrahedral interpolation for high-quality LUT results
        finalPixel = sampleTexture3dTetrahedralInterpolation(colorGradingLutTexture, colorGradeLookupCoord).rgb;
    
        // Direct lookup, i.e. whatever the sampler is set to, e.g. trilinear. This will result in a very low LUT
        // quality for any LUT textures that aren't massive, like in the order of magnitude of 256x256x256.
        //finalPixel = textureLod(colorGradingLutTexture, colorGradeLookupCoord, 0.0).rgb;
    }

    // Apply black bars
    if (gl_FragCoord.x < constants.blackBarsLimits.x || gl_FragCoord.y < constants.blackBarsLimits.y
     || gl_FragCoord.x > constants.blackBarsLimits.z || gl_FragCoord.y > constants.blackBarsLimits.w) {
        finalPixel = vec3(0.0);
    }

    oColor = vec4(finalPixel, 1.0);
}

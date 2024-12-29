#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <common/sampling.glsl>

#include <color/aces.glsl>
#include <color/agx.glsl>
#include <color/khronosPbrNeutral.glsl>
#include <color/st2084.glsl>
#include <color/srgb.glsl>

#include <shared/ColorSpaceData.h>
#include <shared/TonemapData.h>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D finalTexture;
layout(set = 0, binding = 1) uniform sampler2DArray filmGrainTexture;
layout(set = 0, binding = 2) uniform sampler3D colorGradingLutTexture;

NAMED_UNIFORMS(constants,
    int outputColorSpace;
    int tonemapMethod;

    float paperWhiteLm; // for when outputColorSpace is HDR

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
    vec3 color = texture(finalTexture, vTexCoord).rgb;

    // Perform tonemap + OETF
    if (constants.outputColorSpace == COLOR_SPACE_SRGB_NONLINEAR) {

        vec3 hdrColor = color;
        vec3 ldrColor;

        switch (constants.tonemapMethod) {
        case TONEMAP_METHOD_CLAMP:
            ldrColor = clamp(hdrColor, vec3(0.0), vec3(1.0));
            break;
        case TONEMAP_METHOD_REINHARD:
            ldrColor = hdrColor / (hdrColor + vec3(1.0));
            break;
        case TONEMAP_METHOD_ACES:
            ldrColor = ACES_tonemap(hdrColor);
            break;
        case TONEMAP_METHOD_AGX:
            ldrColor = AgX_tonemap(hdrColor);
            break;
        case TONEMAP_METHOD_KHRONOS_PBR_NEUTRAL:
            ldrColor = khronosPbrNeutralTonemap(hdrColor);
            break;
        default:
            ldrColor = vec3(1.0, 0.0, 1.0);
            break;
        }

        // Do "nonlinear" OETF encoding
        color = sRGB_gammaEncode(ldrColor);

        // TODO: Add some dithering before writing to RGBA8 target(?)

    } else if (constants.outputColorSpace == COLOR_SPACE_HDR10_ST2084) {

        // Convert to Rec.2020
        // Note that Rec.709 has the same primaries as sRGB so this is valid
        color = Rec2020_from_Rec709 * color;

        const float ST2084MaxLm = 10000.0;
        color *= constants.paperWhiteLm / ST2084MaxLm;

        color = perceptualQuantizerOETF(color);

    }

    // Apply color grading
    if (constants.colorGrade) {
        vec3 colorGradeLookupCoord = clamp(color, vec3(0.0), vec3(1.0));

        // Tetrahedral interpolation for high-quality LUT results
        color = sampleTexture3dTetrahedralInterpolation(colorGradingLutTexture, colorGradeLookupCoord).rgb;
    
        // Direct lookup, i.e. whatever the sampler is set to, e.g. trilinear. This will result in a very low LUT
        // quality for any LUT textures that aren't massive, like in the order of magnitude of 256x256x256.
        //color = textureLod(colorGradingLutTexture, colorGradeLookupCoord, 0.0).rgb;
    }

    // If we wanted to apply some kind of sharpening filter, this could be a good place to do it "inline", i.e. in a single pass
    // without writing out intermediate results to a texture, by simply sampling with the EOTF & color grading applied.

    // Apply natural vignette (NOTE: this is "natural" vignette, i.e. caused by the angle of incident light striking
    // the sensor. It is a very discrete effect and you shouldn't expect to see a massive difference from it.
    {
        vec2 pixelFromCenter = vTexCoord - vec2(0.5);
        pixelFromCenter.x *= constants.aspectRatio;
        float dist = length(pixelFromCenter) * constants.vignetteIntensity;
        float falloffFactor = 1.0 / square(square(dist) + 1.0);
        color.rgb *= falloffFactor;
    }

    // Apply film grain
    vec2 filmGrainUv = gl_FragCoord.xy / (vec2(textureSize(filmGrainTexture, 0).xy) * constants.filmGrainScale);
    vec3 lookupCoord = vec3(filmGrainUv, float(constants.filmGrainArrayIdx));
    vec3 filmGrain01 = textureLod(filmGrainTexture, lookupCoord, 0.0).rgb;
    vec3 filmGrain = vec3(constants.filmGrainGain * (2.0 * filmGrain01 - 1.0));
    filmGrain *= pow(1.0 - luminance(color), 25.0);
    color = clamp(color + filmGrain, vec3(0.0), vec3(1.0));

    // Apply black bars
    if (gl_FragCoord.x < constants.blackBarsLimits.x || gl_FragCoord.y < constants.blackBarsLimits.y
     || gl_FragCoord.x > constants.blackBarsLimits.z || gl_FragCoord.y > constants.blackBarsLimits.w) {
        color = vec3(0.0);
    }

    oColor = vec4(color, 1.0);
}

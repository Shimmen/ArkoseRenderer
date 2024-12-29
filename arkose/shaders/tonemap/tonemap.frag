#version 460

#include <color/aces.glsl>
#include <color/agx.glsl>
#include <color/khronosPbrNeutral.glsl>
#include <color/srgb.glsl>
#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <shared/TonemapData.h>

layout(set = 0, binding = 0) uniform sampler2D uTexture;

NAMED_UNIFORMS(constants,
    int tonemapMethod;
)

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 hdrColor = texelFetch(uTexture, ivec2(gl_FragCoord.xy), 0).rgb;

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

    vec3 nonlinearLdrColor = sRGB_gammaEncode(ldrColor);

    // TODO: Add some dithering before writing to RGBA8 target!

    // (Needed for FXAA)
    float nonlinearLuma = luminance(nonlinearLdrColor);

    oColor = vec4(nonlinearLdrColor, nonlinearLuma);
}

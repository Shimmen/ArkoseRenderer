#version 460

#define FXAA_PC 1
#define FXAA_GLSL_130 1

//#define FXAA_QUALITY__PRESET 12 // (default quality)
#define FXAA_QUALITY__PRESET 39 // (highest quality)

#include <fxaa/Fxaa3_11.h>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(push_constant) uniform PushConstants {
    //
    // This must be from a constant/uniform.
    // {x_} = 1.0/screenWidthInPixels
    // {_y} = 1.0/screenHeightInPixels
    FxaaFloat2 fxaaQualityRcpFrame;
    //
    // Choose the amount of sub-pixel aliasing removal.
    // This can effect sharpness.
    //   1.00 - upper limit (softer)
    //   0.75 - default amount of filtering
    //   0.50 - lower limit (sharper, less sub-pixel aliasing removal)
    //   0.25 - almost off
    //   0.00 - completely off
    FxaaFloat fxaaQualitySubpix;
    //
    // The minimum amount of local contrast required to apply algorithm.
    //   0.333 - too little (faster)
    //   0.250 - low quality
    //   0.166 - default
    //   0.125 - high quality
    //   0.063 - overkill (slower)
    FxaaFloat fxaaQualityEdgeThreshold;
    //
    // Trims the algorithm from processing darks.
    //   0.0833 - upper limit (default, the start of visible unfiltered edges)
    //   0.0625 - high quality (faster)
    //   0.0312 - visible limit (slower)
    FxaaFloat fxaaQualityEdgeThresholdMin;
};

layout(location = 0) out vec4 oColor;

void main()
{
    // NOTE:
    //  {rgb_} = color in linear or perceptual color space
    //  {___a} = luma in perceptual color space (not linear)
    FxaaFloat2 pos = vTexCoord;

    // note: unused
    FxaaFloat4 fxaaConsolePosPos;
    //FxaaTex fxaaConsole360TexExpBiasNegOne;
    //FxaaTex fxaaConsole360TexExpBiasNegTwo;
    FxaaFloat4 fxaaConsoleRcpFrameOpt;
    FxaaFloat4 fxaaConsoleRcpFrameOpt2;
    FxaaFloat4 fxaaConsole360RcpFrameOpt2;
    FxaaFloat fxaaConsoleEdgeSharpness;
    FxaaFloat fxaaConsoleEdgeThreshold;
    FxaaFloat fxaaConsoleEdgeThresholdMin;
    FxaaFloat4 fxaaConsole360ConstDir;

    vec4 aaColor = FxaaPixelShader(
        pos, fxaaConsolePosPos, uTexture,
        uTexture,// unused, but must pass something .. fxaaConsole360TexExpBiasNegOne,
        uTexture,// unused, but must pass something .. fxaaConsole360TexExpBiasNegTwo,
        fxaaQualityRcpFrame,
        fxaaConsoleRcpFrameOpt,
        fxaaConsoleRcpFrameOpt2,
        fxaaConsole360RcpFrameOpt2,
        fxaaQualitySubpix,
        fxaaQualityEdgeThreshold,
        fxaaQualityEdgeThresholdMin,
        fxaaConsoleEdgeSharpness,
        fxaaConsoleEdgeThreshold,
        fxaaConsoleEdgeThresholdMin,
        fxaaConsole360ConstDir);
    oColor = vec4(aaColor.rgb, 1.0);
}

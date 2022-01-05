#version 460

#include <common/filmGrain.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D srcTexture;

NAMED_UNIFORMS(pushConstants,
    bool enabled;
    float filmGrainGain;
    uint frameIndex;
)

layout(location = 0) out vec4 oColor;

vec3 performTemporalAA(vec3 srcColor)
{
    // TODO: Implement TAA!
    return srcColor;
}

void main()
{
    vec3 srcColor = texture(srcTexture, vTexCoord).rgb;
    vec3 aaColor = (pushConstants.enabled) ? performTemporalAA(srcColor) : srcColor;

    uvec2 pixelCoord = uvec2(gl_FragCoord.xy);
    uvec2 targetSize = uvec2(textureSize(srcTexture, 0));
    vec3 filmGrain = generateFilmGrain(pushConstants.filmGrainGain, pushConstants.frameIndex, pixelCoord, targetSize);

    vec3 finalColor = applyFilmGrain(aaColor, filmGrain);
    oColor = vec4(finalColor, 1.0);
}

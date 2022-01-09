#version 460

#include <common/filmGrain.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D finalTexture;

NAMED_UNIFORMS(pushConstants,
    float filmGrainGain;
    uint frameIndex;
)

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 finalPixel = texture(finalTexture, vTexCoord).rgb;

    uvec2 pixelCoord = uvec2(gl_FragCoord.xy);
    uvec2 targetSize = uvec2(textureSize(finalTexture, 0));
    vec3 filmGrain = generateFilmGrain(pushConstants.filmGrainGain, pushConstants.frameIndex, pixelCoord, targetSize);

    vec3 finalColor = applyFilmGrain(finalPixel, filmGrain);
    oColor = vec4(finalColor, 1.0);
}

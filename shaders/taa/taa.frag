#version 460

#include <common/noise.glsl>

layout(location = 0) noperspective in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D srcTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    // TODO: Implement TAA!
    vec3 color = texture(srcTexture, vTexCoord).rgb;

    // TODO: Use blue noise (or something even better)
    // TODO: Make filmGrainGain a function of the camera ISO: higher ISO -> more digital sensor noise!
    //float noise = hash_2u_to_1f(uvec2(gl_FragCoord.xy) + frameIndex * uvec2(textureSize(texture, 0)));
    //vec3 filmGrain = vec3(filmGrainGain * (2.0 * noise - 1.0));
    //vec3 color = aaColor.rgb + filmGrain;

    oColor = vec4(color, 1.0);
}

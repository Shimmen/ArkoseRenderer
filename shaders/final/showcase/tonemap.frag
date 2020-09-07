#version 460

#include <common.glsl>
#include <common/aces.glsl>

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 hdrColor = texelFetch(uTexture, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 ldrColor = ACES_tonemap(hdrColor);

    vec3 nonlinearLdrColor = pow(ldrColor, vec3(1.0 / 2.2));
    float nonlinearLuma = luminance(nonlinearLdrColor);

    oColor = vec4(nonlinearLdrColor, nonlinearLuma);
}

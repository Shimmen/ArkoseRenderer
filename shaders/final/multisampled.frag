#version 460
#extension GL_ARB_separate_shader_objects : enable

#include <common/aces.glsl>
#include <common/srgb.glsl>
#include <common/spherical.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vViewRay;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform sampler2D uEnvironment;
layout(set = 1, binding = 2) uniform sampler2DMS uDepth;
layout(set = 1, binding = 3) uniform EnvBlock { float envMultiplier; };

layout(location = 0) out vec4 oColor;

layout(push_constant) uniform PushConstants {
    float exposure;
    int multisampleLevel;
};

void main()
{
    float geoToEnv = 0.0;
    for (int i = 0; i < multisampleLevel; ++i)
        geoToEnv += (texelFetch(uDepth, ivec2(gl_FragCoord.xy), i).r >= (1.0 - 1e-6)) ? 0.0 : 1.0;
    geoToEnv /= float(multisampleLevel);

    vec3 geoColor = texture(uTexture, vTexCoord).rgb;

    vec2 sampleUv = sphericalUvFromDirection(normalize(vViewRay));
    vec3 envColor = texture(uEnvironment, sampleUv).rgb;
    //envColor *= envMultiplier;

    vec3 hdrColor = mix(envColor, geoColor, geoToEnv);

    hdrColor *= exposure;
    vec3 ldrColor = ACES_tonemap(hdrColor);
    ldrColor = sRGB_gammaEncode(ldrColor);

    oColor = vec4(ldrColor, 1.0);
}

#version 460

#include <common/brdf.glsl>
#include <common/lighting.glsl>
#include <shared/LightData.h>

DeclareCommonBindingSet_Light(4)

layout(set = 5, binding = 0) uniform sampler2D directionalLightShadowMapTex;
layout(set = 5, binding = 1) uniform sampler2D directionalLightProjectedShadowTex;
layout(set = 5, binding = 2) uniform sampler2D localLightShadowMapAtlasTex;
layout(set = 5, binding = 3) buffer readonly ShadowMapViewportBlock { vec4 localLightShadowMapViewports[]; };

layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 oColor;

float evaluateDirectionalLightShadow(mat4 lightProjectionFromView, vec3 viewSpacePos)
{
    vec4 posInShadowMap = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInShadowMap.xyz /= posInShadowMap.w;

    vec2 shadowMapUv = (posInShadowMap.xy * 0.5 + 0.5);

    float mapDepth = texture(directionalLightShadowMapTex, shadowMapUv).x;
    return (mapDepth < posInShadowMap.z) ? 0.0 : 1.0;
}

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 V, vec3 hairColor)
{
    // TODO: Implement hair lighting!

    vec3 L = -normalize(light.viewSpaceDirection.xyz);
    vec3 brdf = vec3(hairColor); //evaluateHairBRDF(L, V, hairColor);

    float shadowFactor = 0.25;//evaluateDirectionalLightShadow(light.lightProjectionFromView, vPosition);
    vec3 directLight = light.color * shadowFactor;

    return brdf * directLight;
}

void main() 
{
    vec3 hairColor = vColor.rgb;
    float opacity = vColor.a;

    vec3 color = vec3(0.0); // TODO: Ambient light

    if (light_hasDirectionalLight()) {
        color += evaluateDirectionalLight(light_getDirectionalLight(), normalize(vPosition), hairColor);
    }

    oColor = vec4(color, opacity);
}

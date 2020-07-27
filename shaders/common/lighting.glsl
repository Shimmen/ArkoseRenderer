#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include <common.glsl>
#include <shared/LightData.h>
#include <common/brdf.glsl>

vec3 evaluateDirectionalLight(DirectionalLightData light, vec3 baseColor, vec3 L, vec3 N, float shadowFactor)
{
    vec3 lightColor = light.colorAndIntensity.a * light.colorAndIntensity.rgb;

    vec3 directLight = lightColor * shadowFactor;
    float LdotN = max(dot(L, N), 0.0);

    return baseColor * diffuseBRDF() * LdotN * directLight;
}

#endif // LIGHTING_GLSL

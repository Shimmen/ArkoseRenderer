#ifndef BRDF_GLSL
#define BRDF_GLSL

#include <common.glsl>

#define DIELECTRIC_REFLECTANCE (0.04)

//
// The current BRDF model in use is based on the one from Filament,
// documented here: https://google.github.io/filament/Filament.md#toc4.
// It's licenced under the Apache License 2.0 which can be found
// here: https://github.com/google/filament/blob/master/LICENSE.
//

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    float x = a2 / (PI * f * f + 1e-20);
    return x;
}

vec3 F_Schlick(float VdotH, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - VdotH, 5.0);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float GGXL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
    float GGXV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    return 0.5 / (GGXV + GGXL + 1e-20);
}

vec3 specularBRDF(vec3 L, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, out vec3 F)
{
    vec3 H = normalize(L + V);

    float NdotV = abs(dot(N, V)) + 1e-5;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float LdotH = clamp(dot(L, H), 0.0, 1.0);

    // Use a which is perceptually linear for roughness
    float a = square(roughness * roughness);

    vec3 f0 = mix(vec3(DIELECTRIC_REFLECTANCE), baseColor, metallic);

    F = F_Schlick(LdotH, f0);
    float D = D_GGX(NdotH, a);
    float V_ = V_SmithGGXCorrelated(NdotV, NdotL, a);

    return F * D * V_;
}

vec3 diffuseBRDF()
{
    return vec3(1.0) / vec3(PI);
}

vec3 evaluateBRDF(vec3 L, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic)
{
    vec3 F;
    vec3 specular = specularBRDF(L, V, N, baseColor, roughness, metallic, F);

    vec3 diffuseColor = vec3(1.0 - metallic) * baseColor;
    vec3 diffuse = diffuseColor * (1.0 - F) * diffuseBRDF();

    vec3 brdf = diffuse + specular;
    return brdf;
}

#endif // BRDF_GLSL

#ifndef BRDF_GLSL
#define BRDF_GLSL

#include <common.glsl>

#define DIELECTRIC_REFLECTANCE (0.04)

//
// The current BRDF model in use is based on the one from Filament,
// documented here: https://google.github.io/filament/Filament.md#toc4.
// It's licenced under the Apache License 2.0 which can be found
// here: https://github.com/google/filament/blob/master/LICENSE.
// With some supplemental information from here:
// https://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html.
//

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    float x = a2 / (PI * f * f + 1e-20);
    return x;
}

float F_Schlick(float VdotH, float f0) {
    return f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);
}

vec3 F_Schlick(float VdotH, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - VdotH, 5.0);
}

float G1_GGX(float NdotV, float a2)
{
    return (2.0 * NdotV) / (NdotV + sqrt(a2 + (1.0 - a2) * square(NdotV)));
}

float G_GGX(float NdotV, float NdotL, float a)
{
    float a2 = a * a;
    return G1_GGX(NdotV, a2) * G1_GGX(NdotL, a2);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float GGXL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
    float GGXV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    return 0.5 / (GGXV + GGXL + 1e-20);
}

float V_Kelemen(float LdotH)
{
    return 0.25 / square(LdotH);
}

float clearcoatBRDF(vec3 L, vec3 V, vec3 N, float clearcoatStrength, float clearcoatRoughness, out float F)
{
    vec3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float LdotH = saturate(dot(L, H));

    float a = square(clamp(clearcoatRoughness, 0.1, 1.0));

    float D = D_GGX(NdotH, a);
    float V_ = V_Kelemen(LdotH);
    F = F_Schlick(LdotH, DIELECTRIC_REFLECTANCE) * clearcoatStrength;

    return D * V_ * F;
}

vec3 specularBRDF(vec3 L, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, /*float clearcoat, float clearcoatRoughness,*/ out vec3 F)
{
    vec3 H = normalize(L + V);

    float NdotV = abs(dot(N, V)) + 1e-5;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float LdotH = clamp(dot(L, H), 0.0, 1.0);

    // Use `a` which is perceptually linear for roughness
    float a = square(roughness);

    vec3 f0 = mix(vec3(DIELECTRIC_REFLECTANCE), baseColor, metallic);

    F = F_Schlick(LdotH, f0);
    float D = D_GGX(NdotH, a);
    float V_ = V_SmithGGXCorrelated(NdotV, NdotL, a);

    return F * D * V_;
}

//
// "Sampling the GGX Distribution of Visible Normals"
//   https://jcgt.org/published/0007/04/01/paper.pdf
//
// Input Ve: view direction
// Input alpha_x, alpha_y: roughness parameters
// Input U1, U2: uniform random numbers
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
vec3 sampleGGXVNDF(vec3 Ve, float alpha_x, float alpha_y, float U1, float U2)
{
    // Section 3.2: transforming the view direction to the hemisphere configuration
    vec3 Vh = normalize(vec3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1,0,0);
    vec3 T2 = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);
    float phi = 2.0 * PI * U2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s)*sqrt(1.0 - t1*t1) + s*t2;
    // Section 4.3: reprojection onto hemisphere
    vec3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2))*Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    vec3 Ne = normalize(vec3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;
}

vec3 sampleSpecularBRDF(vec3 wo, float roughness, vec2 rand)
{
    float alpha = square(roughness);
    return sampleGGXVNDF(wo, alpha, alpha, rand.x, rand.y);
}

vec3 diffuseBRDF()
{
    return vec3(1.0) / vec3(PI);
}

vec3 evaluateDefaultBRDF(vec3 L, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, float clearcoat, float clearcoatRoughness)
{
    // TODO: Optimize this - there's plenty of redundant calculations here
    // (for example, lots of overlap between clearcoat and specular BRDFs)

    float F_c;
    float Fr_c = clearcoatBRDF(L, V, N, clearcoat, clearcoatRoughness, F_c);

    vec3 F_s;
    vec3 Fr_s = specularBRDF(L, V, N, baseColor, roughness, metallic, F_s);

    vec3 diffuseColor = vec3(1.0 - metallic) * baseColor;
    vec3 Fr_d = diffuseColor * diffuseBRDF();

    vec3 brdf = (Fr_d * (1.0 - F_s) + Fr_s) * (1.0 - F_c) + Fr_c;
    return brdf;
}

vec3 evaluateSkinSpecularBRDF(vec3 L, vec3 V, vec3 N, vec3 albedo, float roughness, out vec3 outF)
{
    const float metallic = 0.0; // skin is not metallic

    vec3 F_s;
    vec3 Fr_s = specularBRDF(L, V, N, albedo, roughness, metallic, F_s);

    outF = F_s;
    return Fr_s;
}

vec3 evaluateSkinBRDF(vec3 L, vec3 V, vec3 N, vec3 albedo, float roughness)
{
    vec3 F_s;
    vec3 Fr_s = evaluateSkinSpecularBRDF(L, V, N, albedo, roughness, F_s);

    vec3 Fr_d = albedo * diffuseBRDF();

    vec3 brdf = Fr_d * (1.0 - F_s) + Fr_s;

    return brdf;
}

vec3 evaluateGlassBRDF(vec3 L, vec3 V, vec3 N, float roughness)
{
    vec3 F;
    return specularBRDF(L, V, N, vec3(1.0), roughness, 0.0, F);
}

#endif // BRDF_GLSL

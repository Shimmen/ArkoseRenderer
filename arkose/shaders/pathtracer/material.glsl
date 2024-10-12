#ifndef PATHTRACER_MATERIAL_GLSL
#define PATHTRACER_MATERIAL_GLSL

#include <common/brdf.glsl>
#include <pathtracer/common.glsl>

// NOTE: All of this is in tangent space unless indicated otherwise.

#define IOR_AIR (1.0)
#define IOR_GLASS (1.5)

struct PathTraceMaterial {
    vec3 baseColor;
    float roughness;
    float metallic;
};

vec3 sampleCosineWeightedHemisphere(float r1, float r2)
{
    float phi = 2.0 * PI * r1;
    float sqrtR2 = sqrt(r2);

    return vec3(cos(phi) * sqrtR2,
                sin(phi) * sqrtR2, 
                sqrt(1.0 - r2));
}

vec3 evaluateLambertian(PathTraceMaterial material, vec3 L, out float PDF)
{
    if (L.z <= 0.0) {
        PDF = 0.0;
        return vec3(0.0);
    }

    PDF = L.z / PI;
    return material.baseColor * vec3(1.0 / PI);
}

vec3 evaluateMicrofacet(PathTraceMaterial material, vec3 V, vec3 L, vec3 F, out float PDF)
{
    if (L.z <= 0.0) {
        PDF = 0.0;
        return vec3(0.0);
    }

    vec3 H = (V + L) * 0.5;

    // From https://jcgt.org/published/0007/04/01/paper.pdf
    // and https://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
    float a = square(material.roughness);
    float D = D_GGX(H.z, max(a, 1e-10));
    float G1 = G1_GGX(V.z, a);
    float G2 = G1 * G1_GGX(L.z, a);

    // Assuming V.z >= 0, we can simplify this (is that a sound assumption?):
    float D_v = G1 * max(0.0, V.z) * D / V.z;
    //float D_v = G1 * D;

    PDF = D_v / (4.0 * V.z);

    return F * D * G2 / (4.0 * L.z * V.z);
}

float powerHeuristic(float fPdf, float gPdf) {
    return square(fPdf) / (square(fPdf) + square(gPdf));
}

vec3 evaluateOpaqueMicrofacetMaterial(inout PathTracerRayPayload payload, PathTraceMaterial material, vec3 V, vec3 L, vec3 F, out float PDF)
{
    PDF = 0.0;
    vec3 f = vec3(0.0);

    float reflectance = luminance(F);

    float metalWeight = material.metallic;
    float dielectricWeight = (1.0 - material.metallic);

    float diffuseReflectionProb = dielectricWeight * luminance(material.baseColor);
    float microfacetReflectionProb = reflectance;

    // Normalize probabilities
    float invTotalWeight = 1.0 / (diffuseReflectionProb + microfacetReflectionProb);
    diffuseReflectionProb *= invTotalWeight;
    microfacetReflectionProb *= invTotalWeight;

    bool reflected = L.z * V.z > 0.0;

    // Diffuse reflection
    if (diffuseReflectionProb > 0.0 && reflected) {
        float lambertialPdf;
        f += evaluateLambertian(material, L, lambertialPdf) * dielectricWeight;
        PDF += lambertialPdf * diffuseReflectionProb;
    }

    // Microfacet reflection
    if (microfacetReflectionProb > 0.0 && reflected) {
        float microfacetPdf;
        float microfacetWeight = dielectricWeight + metalWeight; // (always 1.0 for now)
        f += evaluateMicrofacet(material, V, L, F, microfacetPdf) * microfacetWeight;
        PDF += microfacetPdf * microfacetReflectionProb;
    }

    return f * abs(L.z);
}

vec3 sampleOpaqueMicrofacetMaterial(inout PathTracerRayPayload payload, PathTraceMaterial material, vec3 V, out vec3 L, out float PDF)
{
    if (V.z <= 0.0) {
        PDF = 0.0;
        return vec3(0.0);
    }

    float r0 = pt_randomFloat(payload);
    bool isMetal = material.metallic > r0;

    vec3 f0 = mix(vec3(DIELECTRIC_REFLECTANCE), material.baseColor, isMetal ? 1.0 : 0.0);
    vec3 F = F_Schlick(V.z, f0);
    float reflectance = luminance(F);

    float r1 = pt_randomFloat(payload);
    if (reflectance > r1) {
        // Reflect over microfacet

        float r2 = pt_randomFloat(payload);
        float r3 = pt_randomFloat(payload);

        vec2 a = vec2(square(material.roughness)); // isotropic, for now
        vec3 H = sampleGGXVNDF(V, a.x, a.y, r2, r3);
        L = reflect(-V, H);

    } else if (isMetal) {
        // Not reflected and is a metal, so it's absorbed and won't scatter

        PDF = 0.0; // kill path
        return vec3(0.0);

    } else {
        // Refract, i.e. Lambertian diffuse

        float r2 = pt_randomFloat(payload);
        float r3 = pt_randomFloat(payload);

        L = sampleCosineWeightedHemisphere(r2, r3);

    }

    return evaluateOpaqueMicrofacetMaterial(payload, material, V, L, F, PDF);
}

vec3 samplePolishedGlassMaterial(inout PathTracerRayPayload payload, PathTraceMaterial material, float absorptionFactor, vec3 V, out vec3 L, out float PDF)
{
    vec3 F = F_Schlick(V.z, vec3(DIELECTRIC_REFLECTANCE));
    float reflectance = F.x;

    const vec3 Z = vec3(0.0, 0.0, 1.0); // i.e., N in tangent-space

    const float eta = payload.insideGlass
        ? (IOR_GLASS / IOR_AIR)
        : (IOR_AIR / IOR_GLASS);

    // TODO: This is not the optimal code for this
    vec3 L_refracted = refract(-V, Z, eta);
    bool cantRefract = lengthSquared(L_refracted) < 1e-2f;

    float r1 = pt_randomFloat(payload);
    if (cantRefract || reflectance > r1) {
        // Reflect over normal
        L = reflect(-V, Z);
    } else {
        // Refract through the surface
        L = L_refracted;
        payload.insideGlass != payload.insideGlass;
    }

    PDF = 1.0; // really we want it to not evaluate PDF at all, as it's not a sampled/probabilistic direction
    return payload.insideGlass ? vec3(1.0) : vec3(1.0 - absorptionFactor);
}

#endif // PATHTRACER_MATERIAL_GLSL

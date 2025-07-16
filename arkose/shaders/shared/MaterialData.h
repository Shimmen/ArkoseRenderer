#ifndef MATERIAL_DATA_H
#define MATERIAL_DATA_H

// NOTE: Ensure this matches the BRDF enum values in Brdf.h
#define BRDF_DEFAULT 0
#define BRDF_SKIN    1

struct ShaderMaterial {
    int baseColor;
    int normalMap;
    int metallicRoughness;
    int emissive;

    int occlusion;
    int bentNormalMap;
    float clearcoat;
    float clearcoatRoughness;

    int blendMode;
    float maskCutoff;
    float metallicFactor; // multiplied by value in texture
    float roughnessFactor; // multiplied by value in texture

    vec3 emissiveFactor; // multiplied by value in texture
    int brdf;

    float dielectricReflectance; // F0
    float _unused0;
    float _unused1;
    float _unused2;

    vec4 colorTint;
};

#endif // MATERIAL_DATA_H

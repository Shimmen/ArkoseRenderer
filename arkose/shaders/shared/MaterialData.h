#ifndef MATERIAL_DATA_H
#define MATERIAL_DATA_H

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
    int _unused0;

    vec4 colorTint;
};

#endif // MATERIAL_DATA_H

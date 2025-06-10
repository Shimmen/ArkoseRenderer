#ifndef MATERIAL_DATA_H
#define MATERIAL_DATA_H

struct ShaderMaterial {
    int baseColor;
    int normalMap;
    int metallicRoughness;
    int emissive;

    int occlusion;
    int _unused0;
    int _unused1;
    int _unused2;

    int blendMode;
    float maskCutoff;
    float metallicFactor; // multiplied by value in texture
    float roughnessFactor; // multiplied by value in texture

    vec3 emissiveFactor; // multiplied by value in texture
    int bentNormalMap;

    vec4 colorTint;
};

#endif // MATERIAL_DATA_H

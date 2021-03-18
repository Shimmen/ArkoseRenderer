#ifndef SCENE_DATA_H
#define SCENE_DATA_H

#define SCENE_MAX_TEXTURES 256
#define SCENE_MAX_SHADOW_MAPS 16
#define SCENE_MAX_IES_LUT 16

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    int materialIndex;
    int pad1, pad2, pad3;
};

struct ShaderMaterial {
    int baseColor;
    int normalMap;
    int metallicRoughness;
    int emissive;
};

#endif // SCENE_DATA_H

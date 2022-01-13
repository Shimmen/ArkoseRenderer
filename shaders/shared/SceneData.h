#ifndef SCENE_DATA_H
#define SCENE_DATA_H

#define SCENE_MAX_TEXTURES 256
#define SCENE_MAX_SHADOW_MAPS 16
#define SCENE_MAX_IES_LUT 16

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    mat4 previousFrameWorldFromLocal;
    int materialIndex;
    int materialBlendMode; // shortcut, useful when culling
    int _pad0, _pad1;
};

struct ShaderMaterial {
    int baseColor;
    int normalMap;
    int metallicRoughness;
    int emissive;

    int blendMode;
    float maskCutoff;
    float _pad0, _pad1;
};

struct IndirectShaderDrawable {
    ShaderDrawable drawable;
    vec4 localBoundingSphere;

    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    int _pad0;
};

#endif // SCENE_DATA_H

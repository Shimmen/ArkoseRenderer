#ifndef SCENE_DATA_H
#define SCENE_DATA_H

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    mat4 previousFrameWorldFromLocal;
    int materialIndex;
    int _pad0, _pad1, _pad2;
};

struct ShaderMaterial {
    int baseColor;
    int normalMap;
    int metallicRoughness;
    int emissive;

    int blendMode;
    float maskCutoff;
    float _pad0, _pad1;

    vec4 colorTint;
};

struct IndirectShaderDrawable {
    ShaderDrawable drawable;
    vec4 localBoundingSphere;

    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    int materialBlendMode; // shortcut, useful for culling
};

#endif // SCENE_DATA_H

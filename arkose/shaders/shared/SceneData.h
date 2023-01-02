#ifndef SCENE_DATA_H
#define SCENE_DATA_H

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    mat4 previousFrameWorldFromLocal;
    int materialIndex;
    uint materialSortKey; // TODO: Unused, for now!
    uint firstMeshlet;
    uint meshletCount;
};

struct ShaderMeshlet {
    uint firstIndex;
    uint triangleCount; // TODO: Make u8? Should never have more than 256 triangles! But we need the padding anyway..
    uint materialIndex;
    uint transformIndex;

    vec3 center;
    float radius;
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

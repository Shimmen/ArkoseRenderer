#ifndef SCENE_DATA_H
#define SCENE_DATA_H

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    mat4 previousFrameWorldFromLocal;
    vec4 localBoundingSphere;
    int materialIndex;
    uint materialSortKey; // TODO: Unused, for now!
    uint firstMeshlet;
    uint meshletCount;
};

struct ShaderMeshlet {
    uint firstIndex;
    uint triangleCount; // TODO: Make u8? Should never have more than 256 triangles! But we need the padding anyway..
    uint firstVertex;
    uint vertexCount;

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
    float metallicFactor; // multiplied by value in texture
    float roughnessFactor; // multiplied by value in texture

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

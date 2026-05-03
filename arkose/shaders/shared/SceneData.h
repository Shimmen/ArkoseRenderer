#ifndef SCENE_DATA_H
#define SCENE_DATA_H

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    mat4 previousFrameWorldFromLocal;
    vec4 localBoundingSphere;
    int materialIndex;
    uint drawKey;
    uint firstMeshlet;
    uint meshletCount;
    int relativeVelocityVertex;
    int _pad0, _pad1, _pad2;
};

struct ShaderMeshlet {
    uint firstIndex;
    uint triangleCount; // NOTE: Only the bottom 8-bits are currently in use!
    uint firstVertex;
    uint vertexCount;

    vec3 center;
    float radius;
};

struct NonPositionVertex {
    vec2 texcoord0;
    vec3 normal;
    vec4 tangent;
};

struct SkinningVertex {
    uvec4 jointIndices;
    vec4 jointWeights;
};

struct MorphTargetVertex {
    vec3 position;
    vec3 normal;
    vec3 tangent;
};

#endif // SCENE_DATA_H

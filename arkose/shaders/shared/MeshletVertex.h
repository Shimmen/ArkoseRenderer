#ifndef MESHLET_VERTEX_H
#define MESHLET_VERTEX_H

struct MeshletNonPositionVertex {
    vec2 texcoord0;
    vec3 normal;
    vec4 tangent;
};

struct MeshletSkinningVertex {
    uvec4 jointIndices; // TODO: Pack to u16?!
    vec4 jointWeights;
};

#endif // MESHLET_VERTEX_H

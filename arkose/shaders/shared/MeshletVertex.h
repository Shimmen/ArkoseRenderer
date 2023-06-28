#ifndef MESHLET_VERTEX_H
#define MESHLET_VERTEX_H

// TODO: Combine with the other vertex types! This is now the same as it is for ray tracing and everything else!
struct MeshletNonPositionVertex {
    vec2 texcoord0;
    vec3 normal;
    vec4 tangent;
};

#endif // MESHLET_VERTEX_H

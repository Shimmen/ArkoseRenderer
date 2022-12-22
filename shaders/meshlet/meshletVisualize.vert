#version 460

#include <common.glsl>
#include <common/camera.glsl>
#include <common/randon.glsl>

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 1, binding = 0) buffer restrict InstanceBlock { ShaderDrawable instances[]; };

layout(set = 2, binding = 0) uniform IndexBufferBlock { uvec2 indices[]; };
layout(set = 2, binding = 1) uniform PositionBufferBlock { vec2 positions[]; };

layout(location = 0) out vec3 vColor;

void main()
{
    uvec2 combinedIdx = indices[gl_VertexID];
    uint vertexIdx = combinedIdx.x;
    uint instanceIdx = combinedIdx.y;

    ShaderDrawable instance = instances[instanceIdx];

    // TODO: Would be nice to get meshlet index here too for debug drawing!
    // TODO: We should pass the meshlet index instead of the instance index, as the ShaderMeshlet contains material & transform indices.
    seedRandom(instanceIdx);
    vColor = vec3(randomFloat(), randomFloat(), randomFloat());

    vec4 worldSpacePosition = instance.worldFromLocal * vec4(positions[vertexIdx], 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePosition;
}

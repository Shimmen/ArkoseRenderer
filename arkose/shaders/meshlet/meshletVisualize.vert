#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <common.glsl>
#include <common/camera.glsl>
#include <common/namedUniforms.glsl>

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 1, binding = 0, scalar) buffer restrict readonly MeshletVertIndirBlock { uint meshletVertexIndirection[]; };
layout(set = 1, binding = 1, scalar) buffer restrict readonly PositionsBlock { vec3 vertexPositions[]; };

NAMED_UNIFORMS(constants,
    mat4 worldFromLocal;
    vec3 meshletColor;
)

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = constants.meshletColor;

    uint vertexIdx = meshletVertexIndirection[gl_VertexIndex];
    vec3 localPosition = vertexPositions[vertexIdx];

    vec4 worldSpacePosition = constants.worldFromLocal * vec4(localPosition, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePosition;
}

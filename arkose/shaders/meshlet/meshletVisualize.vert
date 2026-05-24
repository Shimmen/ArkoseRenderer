#version 460

#include <common/camera.glsl>
#include <common/geometry.glsl>
#include <common/namedUniforms.glsl>

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

DeclareCommonBindingSet_Geometry(1)

NAMED_UNIFORMS(constants,
    mat4 worldFromLocal;
    vec3 meshletColor;
)

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = constants.meshletColor;

    uint vertexIdx = geom_getMeshletVertexIndirection(gl_VertexIndex);
    vec3 localPosition = geom_getMeshletPosition(vertexIdx);

    vec4 worldSpacePosition = constants.worldFromLocal * vec4(localPosition, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePosition;
}

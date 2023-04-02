#version 460

#include <common.glsl>
#include <common/camera.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

NAMED_UNIFORMS(constants,
    mat4 worldFromLocal;
    vec3 meshletColor;
)

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = constants.meshletColor;

    vec4 worldSpacePosition = constants.worldFromLocal * vec4(aPosition, 1.0);
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldSpacePosition;
}

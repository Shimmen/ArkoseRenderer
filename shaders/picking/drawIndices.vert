#version 460

#include <common/namedUniforms.glsl>
#include <shared/CameraState.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) readonly buffer TransformDataBlock {
    mat4 transforms[];
};

NAMED_UNIFORMS(pushConstants,
    mat4 projectionFromWorld;
)

layout(location = 0) flat out uint vIndex;

void main()
{
    uint objectIndex = gl_InstanceIndex;
    vIndex = objectIndex;

    mat4 worldFromLocal = transforms[objectIndex];
    gl_Position = pushConstants.projectionFromWorld * worldFromLocal * vec4(aPosition, 1.0);
}

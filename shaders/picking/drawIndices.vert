#version 460

#include <common/namedUniforms.glsl>
#include <shared/CameraState.h>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) readonly buffer DrawablesBlock { ShaderDrawable drawables[]; };

NAMED_UNIFORMS(constants,
    mat4 projectionFromWorld;
)

layout(location = 0) flat out uint vIndex;

void main()
{
    uint objectIndex = gl_InstanceIndex;
    vIndex = objectIndex;

    ShaderDrawable drawable = drawables[objectIndex];
    gl_Position = constants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
}

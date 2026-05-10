#version 460

#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) readonly buffer DrawablesBlock { ShaderDrawable drawables[]; };

NAMED_UNIFORMS(constants,
    mat4 projectionFromWorld;
)

layout(location = 0) flat out uint vDrawableIdx;

void main()
{
    uint drawableIdx = gl_InstanceIndex;
    vDrawableIdx = drawableIdx;

    ShaderDrawable drawable = drawables[drawableIdx];
    gl_Position = constants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
}

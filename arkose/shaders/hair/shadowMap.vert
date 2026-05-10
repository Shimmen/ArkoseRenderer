#version 460

#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable drawables[]; };

NAMED_UNIFORMS(constants,
    mat4 projectionFromWorld;
)

void main()
{
    ShaderDrawable drawable = drawables[gl_InstanceIndex];
    gl_Position = constants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
}

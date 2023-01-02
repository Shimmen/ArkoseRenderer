#version 460

#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable drawables[]; };

NAMED_UNIFORMS(pushConstants,
    mat4 projectionFromWorld;
    float depthOffset;
)

void main()
{
    ShaderDrawable drawable = drawables[gl_DrawID];
    gl_Position = pushConstants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
    gl_Position.z += pushConstants.depthOffset;
}

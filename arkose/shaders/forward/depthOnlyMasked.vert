#version 460

#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;

layout(set = 0, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable drawables[]; };

layout(location = 0) flat out int vMaterialIndex;
layout(location = 1) out vec2 vTexCoord;

NAMED_UNIFORMS(pushConstants,
    mat4 projectionFromWorld;
    float depthOffset;
)

void main()
{
    ShaderDrawable drawable = drawables[gl_InstanceIndex];
    vMaterialIndex = drawable.materialIndex;

    vTexCoord = aTexCoord;

    gl_Position = pushConstants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
    gl_Position.z += pushConstants.depthOffset;
}

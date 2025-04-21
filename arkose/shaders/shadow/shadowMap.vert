#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable perObject[]; };

NAMED_UNIFORMS(constants,
    mat4 lightProjectionFromWorld;
)

void main()
{
    int objectIndex = gl_InstanceIndex;
    mat4 worldFromLocal = perObject[objectIndex].worldFromLocal;

    gl_Position = constants.lightProjectionFromWorld * worldFromLocal * vec4(aPosition, 1.0);
}

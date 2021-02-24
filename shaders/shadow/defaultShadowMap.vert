#version 450

#include <common/namedUniforms.glsl>
#include <shared/ShadowData.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform TransformDataBlock
{
    mat4 transforms[SHADOW_MAX_OCCLUDERS];
};

NAMED_UNIFORMS(pushConstants,
    mat4 lightProjectionFromWorld;
)

void main()
{
    int objectIndex = gl_InstanceIndex;
    mat4 worldFromLocal = transforms[objectIndex];
    gl_Position = pushConstants.lightProjectionFromWorld * worldFromLocal * vec4(aPosition, 1.0);
}

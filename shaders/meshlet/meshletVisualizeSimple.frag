#version 460

#include <common/namedUniforms.glsl>

NAMED_UNIFORMS(constants,
    mat4 worldFromLocal;
    vec3 meshletColor;
)

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(constants.meshletColor, 1.0);
}

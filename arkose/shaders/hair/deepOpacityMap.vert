#version 460

#include <common/geometry.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) in vec3 aPosition;

DeclareCommonBindingSet_Geometry(0)

NAMED_UNIFORMS(constants,
    mat4 projectionFromWorld;
    float shadowMapDepthRange;
)

void main()
{
    ShaderDrawable drawable = geom_getDrawable(gl_InstanceIndex);
    gl_Position = constants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
}

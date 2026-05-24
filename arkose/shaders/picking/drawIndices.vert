#version 460

#include <common/geometry.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) in vec3 aPosition;

DeclareCommonBindingSet_Geometry(0)

NAMED_UNIFORMS(constants,
    mat4 projectionFromWorld;
)

layout(location = 0) flat out uint vDrawableIdx;

void main()
{
    uint drawableIdx = gl_InstanceIndex;
    vDrawableIdx = drawableIdx;

    ShaderDrawable drawable = geom_getDrawable(drawableIdx);
    gl_Position = constants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
}

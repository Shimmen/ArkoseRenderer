#version 460

#include <common/geometry.glsl>
#include <common/namedUniforms.glsl>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;

DeclareCommonBindingSet_Geometry(0)

layout(location = 0) flat out int vMaterialIndex;
layout(location = 1) out vec2 vTexCoord;

NAMED_UNIFORMS(pushConstants,
    mat4 projectionFromWorld;
    float depthOffset;
)

void main()
{
    ShaderDrawable drawable = geom_getDrawable(gl_InstanceIndex);
    vMaterialIndex = drawable.materialIndex;

    vTexCoord = aTexCoord;

    gl_Position = pushConstants.projectionFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);
    gl_Position.z += pushConstants.depthOffset;
}

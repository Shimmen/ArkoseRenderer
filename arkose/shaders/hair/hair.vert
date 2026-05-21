#version 460

#include <common/camera.glsl>
#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 2, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable drawables[]; };

layout(location = 0) out vec3 vPosition;
layout(location = 1) out vec4 vColor;

void main()
{
    ShaderDrawable drawable = drawables[gl_InstanceIndex];
    vec4 viewSpacePos = camera.viewFromWorld * drawable.worldFromLocal * vec4(aPosition, 1.0);

    vPosition = viewSpacePos.xyz;
    vColor = aColor;

    gl_Position = camera.projectionFromView * viewSpacePos;
}

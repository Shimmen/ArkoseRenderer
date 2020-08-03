#version 460

#include <shared/CameraState.h>

layout(location = 0) in vec2 aPosition;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vViewRay;

layout(set = 1, binding = 0) uniform CameraStateBlock { CameraState camera; };

void main()
{
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);

    vec4 viewSpacePos = camera.viewFromProjection * gl_Position;
    vViewRay = mat3(camera.worldFromView) * (viewSpacePos.xyz / viewSpacePos.w);
}

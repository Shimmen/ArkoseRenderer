#version 460

#include <common/camera.glsl>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
#if WITH_TEXTURES
layout(location = 2) in vec2 aTexCoord;
#endif

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec3 vColor;
#if WITH_TEXTURES
layout(location = 1) out vec2 vTexCoord;
#endif

void main()
{
    vColor = aColor;
#if WITH_TEXTURES
    vTexCoord = aTexCoord;
#endif

    // NOTE: Debug drawing should always happen after TAA, if present, so we don't want any frustum jitter
    gl_Position = camera.unjitteredProjectionFromView * camera.viewFromWorld * vec4(aPosition, 1.0);
}

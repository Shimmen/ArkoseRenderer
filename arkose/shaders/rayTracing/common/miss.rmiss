#version 460

#include <rayTracing/common/common.glsl>
#include <shared/CameraState.h>

layout(set = 0, binding = 1) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) rayPayloadIn RayPayloadMain payload;

void main()
{
    payload.hitT = camera.zFar + 1.0;
}

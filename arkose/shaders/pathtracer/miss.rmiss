#version 460

#include <pathtracer/common.glsl>
#include <shared/CameraState.h>

layout(set = 0, binding = 1) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) rayPayloadIn PathTracerRayPayload payload;

void main()
{
    // todo: probaby just set it to a very large (or +inf) so we don't need to read camera memory...
    payload.hitT = camera.zFar + 1.0;
}

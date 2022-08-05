#version 460

#include <rayTracing/common/common.glsl>

layout(location = 1) rayPayloadIn RayPayloadShadow payload;

void main()
{
    // We did not hit any geometry on the path to the light source so we're not in shadow
    payload.inShadow = false;
}

#version 460

#include <pathtracer/common.glsl>

layout(location = 1) rayPayloadIn PathTracerShadowRayPayload payload;

void main()
{
    // We did not hit any geometry on the path to the light source so we're not in shadow
    payload.inShadow = false;
}

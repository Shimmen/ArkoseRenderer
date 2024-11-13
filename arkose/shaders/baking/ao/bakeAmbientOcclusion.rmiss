#version 460

#include <common/rayTracing.glsl>

layout(location = 0) rayPayloadIn float payloadHitDistance;

void main()
{
    payloadHitDistance = 1000.0;
}

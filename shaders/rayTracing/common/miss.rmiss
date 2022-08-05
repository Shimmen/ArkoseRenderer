#version 460

#include <rayTracing/common/common.glsl>

layout(location = 0) rayPayloadIn RayPayloadMain payload;

void main()
{
    payload.hitT = HIT_T_MISS;
}

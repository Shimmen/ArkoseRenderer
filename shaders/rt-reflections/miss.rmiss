#version 460

#include <common/rayTracing.glsl>
#include <rt-reflections/common.glsl>

layout(location = 0) rayPayloadIn RayPayload payload;

void main()
{
	payload.hitT = HIT_T_MISS;
}

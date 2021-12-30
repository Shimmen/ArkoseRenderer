#version 460
#extension GL_NV_ray_tracing : require

#include <ddgi/common.glsl>

layout(location = 0) rayPayloadInNV RayPayload payload;

void main()
{
	payload.hitT = HIT_T_MISS;
}

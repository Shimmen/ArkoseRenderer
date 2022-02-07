#version 460

#include <common/rayTracing.glsl>

layout(location = 1) rayPayloadIn bool inShadow;

void main()
{
	inShadow = false;
}

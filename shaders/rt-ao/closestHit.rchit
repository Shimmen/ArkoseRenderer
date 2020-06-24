#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV float hitT;

void main()
{
	hitT = gl_HitTNV;
}

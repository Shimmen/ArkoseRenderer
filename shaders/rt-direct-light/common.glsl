#ifndef RT_DIRECT_LIGHT_COMMON_GLSL
#define RT_DIRECT_LIGHT_COMMON_GLSL

#include <shared/RTData.h>

#define HIT_T_MISS (-1.0)

struct Vertex {
	vec3 normal;
	vec2 texCoord;
};

struct RayPayload {
	vec3 color;
	float hitT;
};

struct PushConstants {
	float ambientAmount;
	float environmentMultiplier;
};

#endif // RT_DIRECT_LIGHT_COMMON_GLSL

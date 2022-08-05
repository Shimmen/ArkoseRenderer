#ifndef RAYTRACING_COMMON_GLSL
#define RAYTRACING_COMMON_GLSL

#include <common/rayTracing.glsl>
#include <shared/RTData.h>

#define HIT_T_MISS (-1.0)

struct Vertex {
    vec3 normal;
    vec2 texCoord;
};

struct RayPayloadMain {
    vec3 color;
    float hitT;
};

struct RayPayloadShadow {
    bool inShadow;
};

struct RayTracingPushConstants {
    float ambientAmount;
    float environmentMultiplier;

    // Well, I hate this, but not sure how to reuse the closest hit shader (where we need ambient amount) if we don't do this
    float parameter1;
    float parameter2;
};

#endif // RAYTRACING_COMMON_GLSL

#ifndef RAYTRACING_COMMON_GLSL
#define RAYTRACING_COMMON_GLSL

#include <common/rayTracing.glsl>
#include <shared/RTData.h>

#ifndef RT_USE_EXTENDED_RAY_PAYLOAD
#define RT_USE_EXTENDED_RAY_PAYLOAD 0
#endif

struct Vertex {
    vec3 normal;
    vec2 texCoord;
};

struct RayPayloadMain {
    vec3 color;
    float hitT;

    #if RT_USE_EXTENDED_RAY_PAYLOAD
        vec3 baseColor;
        vec3 normal;
        float roughness;
        float metallic;
    #endif
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
    float parameter3;
};

#endif // RAYTRACING_COMMON_GLSL

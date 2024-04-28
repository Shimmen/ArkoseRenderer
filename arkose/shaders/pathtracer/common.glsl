#ifndef PATHTRACER_COMMON_GLSL
#define PATHTRACER_COMMON_GLSL

#include <common/rayTracing.glsl>
#include <shared/RTData.h>

struct PathTracerRayPayload {
    vec3 attenuation;
    vec3 color;

    vec3 scatteredDirection;
    float scatteredDirectionPdf;

    float hitT;

    uint rngState;
};

struct PathTracerShadowRayPayload {
    bool inShadow;
};

struct PathTracerPushConstants {
    float environmentMultiplier;
    uint blueNoiseLayerIndex;
};

#endif // PATHTRACER_COMMON_GLSL

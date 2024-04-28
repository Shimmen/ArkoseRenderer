#ifndef PATHTRACER_COMMON_GLSL
#define PATHTRACER_COMMON_GLSL

#include <common/rayTracing.glsl>
#include <common/random.glsl>
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

// Helper functions

float pt_randomFloat(inout PathTracerRayPayload payload)
{
    float value = payload.rngState / 4294967296.0;
    payload.rngState = rand_xorshift(payload.rngState);
    return value;
}

#endif // PATHTRACER_COMMON_GLSL

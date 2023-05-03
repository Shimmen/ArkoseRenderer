#ifndef FORWARD_COMMON_GLSL
#define FORWARD_COMMON_GLSL

struct ForwardPassConstants {
    float ambientAmount;
    vec2 frustumJitterCorrection;
    vec2 invTargetSize;
};

#endif // FORWARD_COMMON_GLSL

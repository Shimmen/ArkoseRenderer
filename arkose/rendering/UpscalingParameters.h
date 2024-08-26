#pragma once

#include <ark/vector.h>
#include "utility/Extent.h"

class Texture;

enum class UpscalingQuality {
    NativeResolution,
    BestQuality,
    GoodQuality,
    Balanced,
    GoodPerformance,
    BestPerformance,
};

struct UpscalingPreferences {
    Extent2D preferredRenderResolution {};
    float preferredSharpening {};
};

struct UpscalingParameters {
    Texture* inputColor {};
    Texture* upscaledColor {};

    Texture* depthTexture {};

    Texture* velocityTexture {};
    bool velocityTextureIsSceneNormalVelocity { false };

    Texture* exposureTexture {}; // for auto exposure
    float preExposure { 1.0f }; // for manual exposure

    vec2 frustumJitterOffset {}; // in pixels, so in range [-0.5, +0.5]

    // NOTE: Will be ignored if the technique doesn't support sharpness built-in
    float sharpness {};

    bool resetAccumulation { false };
};

#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/UpscalingParameters.h"
#include "utility/Extent.h"

class Backend;
class Texture;

enum class ExternalFeatureType {
    None,

    // Upscaling features
    DLSS,
};

enum class ExternalFeatureParameter {
    // DLSS
    DLSS_OptimalMipBias,
    DLSS_OptimalSharpness,
};

class ExternalFeature : public Resource {
public:
    ExternalFeature() = default;
    ExternalFeature(Backend&, ExternalFeatureType);

    ExternalFeatureType type() const { return m_type; }

    virtual float queryParameterF(ExternalFeatureParameter);

private:
    ExternalFeatureType m_type { ExternalFeatureType::None };
};

// External feature structs

struct ExternalFeatureCreateParamsDLSS {
    UpscalingQuality quality;
    Extent2D renderResolution;
    Extent2D outputResolution;
};

struct ExternalFeatureEvaluateParamsDLSS {
    Texture* inputColor {};
    Texture* upscaledColor {};

    Texture* depthTexture {};

    Texture* velocityTexture {};
    bool velocityTextureIsSceneNormalVelocity { false };

    Texture* exposureTexture {}; // for auto exposure
    float preExposure { 1.0f }; // for manual exposure

    vec2 frustumJitterOffset {}; // in pixels, so in range [-0.5, +0.5]

    float sharpness {};

    bool resetAccumulation { false };
};

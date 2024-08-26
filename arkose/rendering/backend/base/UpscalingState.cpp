#include "UpscalingState.h"

#include "rendering/backend/base/Backend.h"

UpscalingState::UpscalingState(Backend& backend, UpscalingTech tech, UpscalingQuality quality, Extent2D renderRes, Extent2D outputRes)
    : Resource(backend)
    , m_tech(tech)
    , m_quality(quality)
    , m_renderResolution(renderRes)
    , m_outputResolution(outputRes)
{
}

float UpscalingState::optimalMipBias() const
{
    float renderResolutionX = static_cast<float>(renderResolution().width());
    float outputResolutionX = static_cast<float>(outputResolution().width());
    float mipBias = std::log2(renderResolutionX / outputResolutionX) - 1.0f;
    return mipBias;
}

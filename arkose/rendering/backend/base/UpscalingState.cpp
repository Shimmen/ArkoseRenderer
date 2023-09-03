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

#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/UpscalingParameters.h"
#include "utility/Extent.h"
#include <optional>

class Backend;
class CommandList;

enum class UpscalingTech {
    None,
#if WITH_DLSS
    DLSS,
#endif
};

class UpscalingState : public Resource {
public:
    UpscalingState() = default;
    UpscalingState(Backend&, UpscalingTech, UpscalingQuality, Extent2D renderRes, Extent2D outputRes);

    UpscalingTech upscalingTech() const { return m_tech; }

    UpscalingQuality quality() const { return m_quality; }
    virtual void setQuality(UpscalingQuality quality) { m_quality = quality; }

    Extent2D renderResolution() const { return m_renderResolution; }
    Extent2D outputResolution() const { return m_outputResolution; }

    bool hasOptimalSharpness() const { return m_optimalSharpness.has_value(); }
    std::optional<float> optimalSharpness() const { return m_optimalSharpness; }

    float optimalMipBias() const;

protected:
    // Can be nullopt, e.g. if the upscaling tech doesn't support sharpness or has no preference.
    std::optional<float> m_optimalSharpness {};

private:
    UpscalingTech m_tech { UpscalingTech::None };
    UpscalingQuality m_quality { UpscalingQuality::Balanced };

    Extent2D m_renderResolution;
    Extent2D m_outputResolution;
};

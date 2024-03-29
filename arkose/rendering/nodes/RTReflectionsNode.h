#pragma once

#include "rendering/RenderPipelineNode.h"

class RTReflectionsNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "RT reflections"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    float noTracingRoughnessThreshold() const { return m_noTracingRoughnessThreshold; }
    void setNoTracingRoughnessThreshold(float threshold) { m_noTracingRoughnessThreshold = threshold; }

private:
    float m_injectedAmbient { 0.0f };

    float m_mirrorRoughnessThreshold { 0.001f };
    float m_noTracingRoughnessThreshold { 0.6f };

    bool m_denoiseEnabled { true };
    
    // FidelityFX denoiser settings
    float m_temporalStability { 0.7f };

    //

    Texture* m_radianceTex { nullptr }; // Ray traced reflections raw output
    Texture* m_resolvedRadianceAndVarianceTex { nullptr }; // Denoised result

    // History textures
    Texture* m_radianceHistoryTex { nullptr };
    Texture* m_worldSpaceNormalHistoryTex { nullptr };
    Texture* m_depthRoughnessVarianceNumSamplesHistoryTex { nullptr };

    // Intermediate textures
    Texture* m_reprojectedRadianceTex { nullptr };
    Texture* m_averageRadianceTex { nullptr };
    Texture* m_varianceTex { nullptr };
    Texture* m_numSamplesTex { nullptr };
    Texture* m_temporalAccumulationTex { nullptr };

    RayTracingState& createRayTracingState(GpuScene&, Registry&, Texture& reflectionsTexture, Texture& reflectionDirectionTex, Texture& blueNoiseTexture) const;

    ComputeState& createDenoiserHistoryCopyState(Registry&);

    ComputeState& createDenoiserReprojectState(Registry&);
    ComputeState& createDenoiserPrefilterState(Registry&);
    ComputeState& createDenoiserTemporalResolveState(Registry&);

};

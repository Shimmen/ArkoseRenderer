#pragma once

#include "rendering/RenderPipelineNode.h"

class RTReflectionsNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "RT reflections"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    float m_injectedAmbient { 500.0f };

    float m_mirrorRoughnessThreshold { 0.001f };
    float m_noTracingRoughnessThreshold { 0.5f };

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

    struct DenoiserPassData {
        DenoiserPassData(ComputeState& inState, BindingSet& inBindings, Extent3D inGlobalSize, Extent3D inLocalSize)
            : state(inState), bindings(inBindings), globalSize(inGlobalSize), localSize(inLocalSize) {}

        ComputeState& state;
        BindingSet& bindings;
        Extent3D globalSize;
        Extent3D localSize;
    };

    DenoiserPassData& createDenoiserHistoryCopyState(Registry&);

    DenoiserPassData& createDenoiserReprojectState(Registry&);
    DenoiserPassData& createDenoiserPrefilterState(Registry&);
    DenoiserPassData& createDenoiserTemporalResolveState(Registry&);

};

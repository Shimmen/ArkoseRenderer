#pragma once

#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/backend/base/BindingSet.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/base/ComputeState.h"
#include "rendering/backend/base/ExternalFeature.h"
#include "rendering/backend/base/RayTracingState.h"
#include "rendering/backend/base/RenderState.h"
#include "rendering/backend/base/RenderTarget.h"
#include "rendering/backend/base/Texture.h"
#include "rendering/backend/base/Sampler.h"
#include "rendering/backend/util/VramStats.h"
#include "core/Badge.h"
#include <memory>
#include <vector>

class RenderPipeline;

class Backend {
private:

    // Only one backend can exist at any point in time
    static Backend* s_globalBackend;

protected:

    Backend() = default;
    virtual ~Backend() = default;

    Backend(Backend&&) = delete;
    Backend(Backend&) = delete;
    Backend& operator=(Backend&) = delete;

public:

    enum class Type {
        Vulkan,
        D3D12,
    };

    enum class Capability {
        RayTracing,
        MeshShading,
        Shader16BitFloat,
        ShaderBarycentrics,
    };

    enum class SwapchainTransferFunction {
        // i.e., using the sRGB / Rec. 709 transfer function
        sRGB_nonLinear,
        // i.e., using the perceptual quantizer (PQ) transfer function
        ST2084,
    };

    struct SubmitStatus {
        void* data;
    };

    struct AppSpecification {
        std::vector<Backend::Capability> requiredCapabilities;
        std::vector<Backend::Capability> optionalCapabilities;
    };

    // Creating and destroying the global backend object
    static Backend& create(Backend::AppSpecification const&);
    static void destroy();

    // Get a reference to the global backend
    static Backend& get();

    static std::string capabilityName(Capability capability);
    virtual bool hasActiveCapability(Capability) const = 0;

    virtual void completePendingOperations() = 0;

    virtual void renderPipelineDidChange(RenderPipeline&) = 0;
    virtual void shadersDidRecompile(const std::vector<std::filesystem::path>& shaderNames, RenderPipeline&) = 0;

    virtual void waitForFrameReady() = 0;
    virtual void newFrame() = 0;
    virtual bool executeFrame(RenderPipeline&, float elapsedTime, float deltaTime) = 0;

    virtual std::optional<SubmitStatus> submitRenderPipeline(RenderPipeline&, Registry&, UploadBuffer&, char const* debugName = nullptr) = 0;
    virtual bool pollSubmissionStatus(SubmitStatus&) const = 0;
    virtual bool waitForSubmissionCompletion(SubmitStatus&, u64 timeout) const = 0;

    virtual int vramStatsReportRate() const { return 0; }
    virtual std::optional<VramStats> vramStats() { return {}; }

    virtual bool hasUpscalingSupport() const = 0;
    virtual UpscalingPreferences queryUpscalingPreferences(UpscalingTech, UpscalingQuality, Extent2D outputRes) const { return UpscalingPreferences(); }

    virtual SwapchainTransferFunction swapchainTransferFunction() const = 0;
    virtual bool hasSrgbTransferFunction() const { return swapchainTransferFunction() == SwapchainTransferFunction::sRGB_nonLinear; }

    virtual std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage) = 0;
    virtual std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) = 0;
    virtual std::unique_ptr<Sampler> createSampler(Sampler::Description) = 0;
    virtual std::unique_ptr<Texture> createTexture(Texture::Description) = 0;
    virtual std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) = 0;
    virtual std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const std::vector<VertexLayout>&, const Shader&, const StateBindings&,
                                                           const RasterState&, const DepthState&, const StencilState&) = 0;
    virtual std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) = 0;
    virtual std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(uint32_t maxInstanceCount) = 0;
    virtual std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) = 0;
    virtual std::unique_ptr<ComputeState> createComputeState(Shader const&, StateBindings const&) = 0;
    virtual std::unique_ptr<ExternalFeature> createExternalFeature(ExternalFeatureType, void* externalFeatureParameters) = 0;

protected:
    Badge<Backend> badge() const { return {}; }

};

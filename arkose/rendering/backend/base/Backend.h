#pragma once

#include "rendering/backend/Resources.h"
#include "rendering/backend/util/VramStats.h"
#include "core/Badge.h"
#include <memory>
#include <vector>

class RenderPipeline;

struct GLFWwindow;

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
    };

    struct AppSpecification {
        std::vector<Backend::Capability> requiredCapabilities;
        std::vector<Backend::Capability> optionalCapabilities;
    };

    // Creating and destroying the global backend object
    static Backend& create(Backend::Type, GLFWwindow*, const Backend::AppSpecification&);
    static void destroy();

    // Get a reference to the global backend
    static Backend& get();

    static std::string capabilityName(Capability capability);
    virtual bool hasActiveCapability(Capability) const = 0;

    virtual ShaderDefine rayTracingShaderDefine() const = 0;

    virtual void completePendingOperations() = 0;

    virtual void renderPipelineDidChange(RenderPipeline&) = 0;
    virtual void shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline&) = 0;

    virtual void newFrame() = 0;
    virtual bool executeFrame(RenderPipeline&, float elapsedTime, float deltaTime) = 0;

    virtual int vramStatsReportRate() const { return 0; }
    virtual std::optional<VramStats> vramStats() { return {}; }

    virtual std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) = 0;
    virtual std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) = 0;
    virtual std::unique_ptr<Texture> createTexture(Texture::Description) = 0;
    virtual std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) = 0;
    virtual std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const std::vector<VertexLayout>&, const Shader&, const StateBindings&,
                                                           const RasterState&, const DepthState&, const StencilState&) = 0;
    virtual std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>, BottomLevelAS const* copySource = nullptr) = 0;
    virtual std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance>) = 0;
    virtual std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) = 0;
    virtual std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<BindingSet*>) = 0;

protected:
    Badge<Backend> badge() const { return {}; }

};

#pragma once

#include "backend/Resources.h"
#include "utility/Badge.h"
#include "utility/util.h"
#include <memory>
#include <vector>

class RenderPipeline;
class Scene;

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    enum class Type {
        Vulkan
    };

    enum class Capability {
        RtxRayTracing,
        Shader16BitFloat,
    };

    struct AppSpecification {
        std::vector<Backend::Capability> requiredCapabilities;
        std::vector<Backend::Capability> optionalCapabilities;
    };

    static std::string capabilityName(Capability capability);
    virtual bool hasActiveCapability(Capability) const = 0;

    virtual Registry& getPersistentRegistry() = 0;

    virtual void renderPipelineDidChange(RenderPipeline&) = 0;
    virtual void shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline&) = 0;

    virtual void newFrame(Scene&) = 0;
    virtual bool executeFrame(const Scene&, RenderPipeline&, double elapsedTime, double deltaTime) = 0;

    virtual std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) = 0;
    virtual std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) = 0;
    virtual std::unique_ptr<Texture> createTexture(Texture::TextureDescription) = 0;
    virtual std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) = 0;
    virtual std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, const StateBindings&,
                                                           const Viewport&, const BlendState&, const RasterState&, const DepthState&, const StencilState&)
        = 0;
    virtual std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) = 0;
    virtual std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>) = 0;
    virtual std::unique_ptr<RayTracingState> createRayTracingState(ShaderBindingTable& sbt, const StateBindings&, uint32_t maxRecursionDepth) = 0;
    virtual std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<BindingSet*>) = 0;

protected:
    Badge<Backend> badge() const { return {}; }

};
#pragma once

#include "rendering/Resources.h"
#include "utility/Badge.h"
#include "utility/util.h"
#include <memory>
#include <vector>

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    enum class Feature {
        TextureArrayDynamicIndexing
    };

    enum class Type {
        Vulkan
    };

    Type type() const { return m_type; }

    virtual bool executeFrame(double elapsedTime, double deltaTime, bool renderGui) = 0;

    virtual std::unique_ptr<Buffer> createBuffer(size_t, Buffer::Usage, Buffer::MemoryHint) = 0;
    virtual std::unique_ptr<RenderTarget> createRenderTarget(std::vector<RenderTarget::Attachment>) = 0;
    virtual std::unique_ptr<Texture> createTexture(Extent2D, Texture::Format, Texture::Usage, Texture::MinFilter, Texture::MagFilter, Texture::Mipmap, Texture::Multisampling) = 0;
    virtual std::unique_ptr<BindingSet> createBindingSet(std::vector<ShaderBinding>) = 0;
    virtual std::unique_ptr<RenderState> createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, std::vector<const BindingSet*>,
                                                           const Viewport&, const BlendState&, const RasterState&, const DepthState&) = 0;
    virtual std::unique_ptr<BottomLevelAS> createBottomLevelAccelerationStructure(std::vector<RTGeometry>) = 0;
    virtual std::unique_ptr<TopLevelAS> createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>) = 0;
    virtual std::unique_ptr<RayTracingState> createRayTracingState(const ShaderBindingTable& sbt, std::vector<const BindingSet*>, uint32_t maxRecursionDepth) = 0;
    virtual std::unique_ptr<ComputeState> createComputeState(const Shader&, std::vector<const BindingSet*>) = 0;

protected:
    // FIXME: Remove this once we no longer have the resource ID lookup system
    [[nodiscard]] static Badge<Backend> backendBadge()
    {
        return {};
    }

private:
    Type m_type;
};

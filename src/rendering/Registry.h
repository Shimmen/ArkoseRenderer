#pragma once

#include "AppState.h"
#include "NodeDependency.h"
#include "backend/Backend.h"
#include "backend/Resources.h"
#include "utility/util.h"
#include <unordered_map>
#include <unordered_set>

class Registry {
public:
    explicit Registry(Backend&, const RenderTarget* windowRenderTarget = nullptr);

    void setCurrentNode(std::string);

    [[nodiscard]] const RenderTarget& windowRenderTarget();
    [[nodiscard]] RenderTarget& createRenderTarget(std::vector<RenderTarget::Attachment>);

    [[nodiscard]] Texture& createPixelTexture(vec4 pixelValue, bool srgb);
    [[nodiscard]] Texture& loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture& createTexture2D(Extent2D, Texture::Format, Texture::Mipmap = Texture::Mipmap::None, Texture::WrapModes = Texture::WrapModes::repeatAll());
    [[nodiscard]] Texture& createMultisampledTexture2D(Extent2D, Texture::Format, Texture::Multisampling, Texture::Mipmap = Texture::Mipmap::None);

    [[nodiscard]] Buffer& createBuffer(size_t size, Buffer::Usage, Buffer::MemoryHint);
    template<typename T>
    [[nodiscard]] Buffer& createBuffer(const std::vector<T>& inData, Buffer::Usage usage, Buffer::MemoryHint);
    [[nodiscard]] Buffer& createBuffer(const std::byte* data, size_t size, Buffer::Usage, Buffer::MemoryHint);

    [[nodiscard]] BindingSet& createBindingSet(std::vector<ShaderBinding>);

    [[nodiscard]] RenderState& createRenderState(const RenderStateBuilder&);
    [[nodiscard]] RenderState& createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, std::vector<const BindingSet*>, const Viewport&, const BlendState&, const RasterState&, const DepthState&);

    [[nodiscard]] BottomLevelAS& createBottomLevelAccelerationStructure(std::vector<RTGeometry>);
    [[nodiscard]] TopLevelAS& createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>);
    [[nodiscard]] RayTracingState& createRayTracingState(const ShaderBindingTable&, std::vector<const BindingSet*>, uint32_t maxRecursionDepth);

    [[nodiscard]] ComputeState& createComputeState(const Shader&, std::vector<const BindingSet*>);

    void publish(const std::string& name, Buffer&);
    void publish(const std::string& name, Texture&);
    void publish(const std::string& name, TopLevelAS&);

    [[nodiscard]] std::optional<Texture*> getTexture(const std::string& renderPass, const std::string& name);
    [[nodiscard]] Buffer* getBuffer(const std::string& renderPass, const std::string& name);
    [[nodiscard]] TopLevelAS* getTopLevelAccelerationStructure(const std::string& renderPass, const std::string& name);

    [[nodiscard]] const std::unordered_set<NodeDependency>& nodeDependencies() const;

    // REMOVE: not needed now/soon, I think..
    [[nodiscard]] Badge<Registry> exchangeBadges(Badge<Backend>) const;

protected:
    std::string makeQualifiedName(const std::string& node, const std::string& name);

private:
    Backend& m_backend;
    Backend& backend() { return m_backend; }

    std::optional<std::string> m_currentNodeName;
    std::unordered_set<NodeDependency> m_nodeDependencies;

    const RenderTarget* m_windowRenderTarget;

    std::unordered_map<std::string, Buffer*> m_nameBufferMap;
    std::unordered_map<std::string, Texture*> m_nameTextureMap;
    std::unordered_map<std::string, TopLevelAS*> m_nameTopLevelASMap;

    std::vector<std::unique_ptr<Buffer>> m_buffers;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<std::unique_ptr<RenderTarget>> m_renderTargets;
    std::vector<std::unique_ptr<BindingSet>> m_bindingSets;
    std::vector<std::unique_ptr<RenderState>> m_renderStates;
    std::vector<std::unique_ptr<BottomLevelAS>> m_bottomLevelAS;
    std::vector<std::unique_ptr<TopLevelAS>> m_topLevelAS;
    std::vector<std::unique_ptr<RayTracingState>> m_rayTracingStates;
    std::vector<std::unique_ptr<ComputeState>> m_computeStates;
};

template<typename T>
[[nodiscard]] Buffer& Registry::createBuffer(const std::vector<T>& inData, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    size_t dataSize = inData.size() * sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(inData.data());
    return createBuffer(binaryData, dataSize, usage, memoryHint);
}

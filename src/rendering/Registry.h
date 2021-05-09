#pragma once

#include "AppState.h"
#include "NodeDependency.h"
#include "backend/Backend.h"
#include "backend/Resources.h"
#include "utility/Image.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <unordered_map>
#include <unordered_set>

class Registry final {
public:
    explicit Registry(Backend&, Registry* previousRegistry, const RenderTarget* windowRenderTarget = nullptr);

    void setCurrentNode(const std::string&);

    [[nodiscard]] const RenderTarget& windowRenderTarget();
    [[nodiscard]] RenderTarget& createRenderTarget(std::vector<RenderTarget::Attachment>);

    [[nodiscard]] Texture& createPixelTexture(vec4 pixelValue, bool srgb);
    [[nodiscard]] Texture& loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture& createTexture2D(Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, Texture::WrapModes = Texture::WrapModes::repeatAll());
    [[nodiscard]] Texture& createTextureArray(uint32_t itemCount, Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, Texture::WrapModes = Texture::WrapModes::repeatAll());
    [[nodiscard]] Texture& createTextureFromImage(const Image&, bool srgb, bool generateMipmaps, Texture::WrapModes = Texture::WrapModes::repeatAll());
    [[nodiscard]] Texture& createMultisampledTexture2D(Extent2D, Texture::Format, Texture::Multisampling, Texture::Mipmap = Texture::Mipmap::None);
    [[nodiscard]] Texture& createCubemapTexture(Extent2D, Texture::Format);

    // TODO: Create more of these reuse variants!
    Texture& createOrReuseTextureArray(const std::string& name, uint32_t itemCount, Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, Texture::WrapModes = Texture::WrapModes::repeatAll());

    [[nodiscard]] Buffer& createBuffer(size_t size, Buffer::Usage, Buffer::MemoryHint);
    [[nodiscard]] Buffer& createBuffer(const std::byte* data, size_t size, Buffer::Usage, Buffer::MemoryHint);
    template<typename T>
    [[nodiscard]] Buffer& createBuffer(const std::vector<T>& inData, Buffer::Usage usage, Buffer::MemoryHint);
    template<typename T>
    [[nodiscard]] Buffer& createBufferForData(const T& inData, Buffer::Usage usage, Buffer::MemoryHint);

    [[nodiscard]] BindingSet& createBindingSet(std::vector<ShaderBinding>);

    [[nodiscard]] RenderState& createRenderState(const RenderStateBuilder&);
    [[nodiscard]] RenderState& createRenderState(const RenderTarget&, const VertexLayout&, const Shader&, std::vector<BindingSet*>, const Viewport&, const BlendState&, const RasterState&, const DepthState&, const StencilState&);

    [[nodiscard]] BottomLevelAS& createBottomLevelAccelerationStructure(std::vector<RTGeometry>);
    [[nodiscard]] TopLevelAS& createTopLevelAccelerationStructure(std::vector<RTGeometryInstance>);
    [[nodiscard]] RayTracingState& createRayTracingState(ShaderBindingTable&, std::vector<BindingSet*>, uint32_t maxRecursionDepth);

    [[nodiscard]] ComputeState& createComputeState(const Shader&, std::vector<BindingSet*>);

    bool hasPreviousNode(const std::string& name) const;

    void publish(const std::string& name, Buffer&);
    void publish(const std::string& name, Texture&);
    void publish(const std::string& name, BindingSet&);
    void publish(const std::string& name, TopLevelAS&);

    [[nodiscard]] std::optional<Texture*> getTexture(const std::string& node, const std::string& name);
    [[nodiscard]] Buffer* getBuffer(const std::string& node, const std::string& name);
    [[nodiscard]] BindingSet* getBindingSet(const std::string& node, const std::string& name);
    [[nodiscard]] TopLevelAS* getTopLevelAccelerationStructure(const std::string& node, const std::string& name);

    [[nodiscard]] Texture* getTextureWithoutDependency(const std::string& node, const std::string& name);

    [[nodiscard]] const std::unordered_set<NodeDependency>& nodeDependencies() const;

    // REMOVE: not needed now/soon, I think..
    [[nodiscard]] Badge<Registry> exchangeBadges(Badge<Backend>) const;

private:
    Backend& m_backend;
    Backend& backend() { return m_backend; }

    Registry* m_previousRegistry { nullptr };

    std::optional<std::string> m_currentNodeName;
    std::unordered_set<NodeDependency> m_nodeDependencies;
    std::vector<std::string> m_allNodes;

    const RenderTarget* m_windowRenderTarget;

    std::unordered_map<std::string, Buffer*> m_nameBufferMap;
    std::unordered_map<std::string, Texture*> m_nameTextureMap;
    std::unordered_map<std::string, BindingSet*> m_nameBindingSetMap;
    std::unordered_map<std::string, TopLevelAS*> m_nameTopLevelASMap;

    std::string makeQualifiedName(const std::string& node, const std::string& name);

    template<typename T>
    void publishResource(const std::string& name, T& resource, std::unordered_map<std::string, T*>& map);

    template<typename T>
    T* getResource(const std::string& node, const std::string& name, const std::unordered_map<std::string, T*>& map);
    template<typename T>
    T* getResourceWithoutDependency(const std::string& node, const std::string& name, const std::unordered_map<std::string, T*>& map);

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

template<typename T>
[[nodiscard]] Buffer& Registry::createBufferForData(const T& inData, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    constexpr size_t dataSize = sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(&inData);
    return createBuffer(binaryData, dataSize, usage, memoryHint);
}

template<typename T>
void Registry::publishResource(const std::string& name, T& resource, std::unordered_map<std::string, T*>& map)
{
    ASSERT(m_currentNodeName.has_value());
    auto nodeName = m_currentNodeName.value();

    if (resource.owningRegistry({}) != this) {
        LogErrorAndExit("Registry: Attempt to publish the resource '%s' in node %s, but the resource is not owned by this registry. "
                        "This could be caused by a per-node resource being published as a per-frame node, or similar.\n",
                        name.c_str(), nodeName.c_str());
    }

    auto fullName = makeQualifiedName(nodeName, name);
    auto entry = map.find(fullName);
    ASSERT(entry == map.end());
    map[fullName] = &resource;
}

template<typename T>
T* Registry::getResource(const std::string& node, const std::string& name, const std::unordered_map<std::string, T*>& map)
{
    T* resource = getResourceWithoutDependency(node, name, map);

    if (resource) {
        ASSERT(m_currentNodeName.has_value());
        NodeDependency dependency { m_currentNodeName.value(), node };
        m_nodeDependencies.insert(dependency);
    }

    return resource;
}

template<typename T>
T* Registry::getResourceWithoutDependency(const std::string& node, const std::string& name, const std::unordered_map<std::string, T*>& map)
{
    std::string fullName = makeQualifiedName(node, name);
    auto entry = map.find(fullName);
    if (entry == map.end())
        return nullptr;
    T* resource = entry->second;
    return resource;
}

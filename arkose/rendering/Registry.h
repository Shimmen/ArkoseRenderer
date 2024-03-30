#pragma once

#include "core/memory/BumpAllocator.h"
#include "rendering/AppState.h"
#include "rendering/NodeDependency.h"
#include "rendering/backend/base/Backend.h"
#include "rendering/backend/Resources.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include <ark/conversion.h>
#include <unordered_map>
#include <unordered_set>

class RenderPipeline;

class Registry final {
public:
    explicit Registry(Backend&, const RenderTarget& windowRenderTarget, Registry* previousRegistry);

    void setCurrentNode(Badge<RenderPipeline>, std::optional<std::string>);

    [[nodiscard]] const RenderTarget& windowRenderTarget();
    [[nodiscard]] RenderTarget& createRenderTarget(std::vector<RenderTarget::Attachment>);

    [[nodiscard]] Texture& createPixelTexture(vec4 pixelValue, bool srgb);
    [[nodiscard]] Texture& loadTextureArrayFromFileSequence(const std::string& imagePathPattern, bool srgb, bool generateMipmaps);
    [[nodiscard]] Texture& createTexture(Texture::Description const&);
    [[nodiscard]] Texture& createTexture2D(Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, ImageWrapModes = ImageWrapModes::repeatAll());
    [[nodiscard]] Texture& createTextureArray(uint32_t itemCount, Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, ImageWrapModes = ImageWrapModes::repeatAll());
    [[nodiscard]] Texture& createMultisampledTexture2D(Extent2D, Texture::Format, Texture::Multisampling, Texture::Mipmap = Texture::Mipmap::None);
    [[nodiscard]] Texture& createCubemapTexture(Extent2D, Texture::Format);

    enum class ReuseMode {
        Created,
        Reused,
    };

    std::pair<Texture&, ReuseMode> createOrReuseTexture2D(const std::string& name, Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, ImageWrapModes = ImageWrapModes::repeatAll());
    Texture& createOrReuseTextureArray(const std::string& name, uint32_t itemCount, Extent2D, Texture::Format, Texture::Filters = Texture::Filters::linear(), Texture::Mipmap = Texture::Mipmap::None, ImageWrapModes = ImageWrapModes::repeatAll());

    [[nodiscard]] Buffer& createBuffer(size_t size, Buffer::Usage);
    [[nodiscard]] Buffer& createBuffer(const std::byte* data, size_t size, Buffer::Usage);
    template<typename T>
    [[nodiscard]] Buffer& createBuffer(const std::vector<T>& inData, Buffer::Usage usage);
    template<typename T>
    [[nodiscard]] Buffer& createBufferForData(const T& inData, Buffer::Usage usage);

    [[nodiscard]] BindingSet& createBindingSet(std::vector<ShaderBinding>);

    [[nodiscard]] RenderState& createRenderState(const RenderStateBuilder&);
    [[nodiscard]] RenderState& createRenderState(const RenderTarget&, const std::vector<VertexLayout>&, const Shader&, const StateBindings&, const RasterState&, const DepthState&, const StencilState&);

    [[nodiscard]] BottomLevelAS& createBottomLevelAccelerationStructure(std::vector<RTGeometry>);
    [[nodiscard]] TopLevelAS& createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance> initialInstances);
    [[nodiscard]] RayTracingState& createRayTracingState(ShaderBindingTable&, const StateBindings&, uint32_t maxRecursionDepth);

    [[nodiscard]] ComputeState& createComputeState(Shader const&, StateBindings const&);

    [[nodiscard]] UpscalingState& createUpscalingState(UpscalingTech, UpscalingQuality, Extent2D renderRes, Extent2D outputDisplayRes);

    template<typename T, typename... Args>
    [[nodiscard]] T& allocate(Args&&...);

    bool hasPreviousNode(const std::string& name) const;

    void publish(const std::string& name, Buffer&);
    void publish(const std::string& name, Texture&);
    void publish(const std::string& name, BindingSet&);
    void publish(const std::string& name, TopLevelAS&);

    [[nodiscard]] Buffer*     getBuffer(const std::string& name);
    [[nodiscard]] Texture*    getTexture(const std::string& name);
    [[nodiscard]] BindingSet* getBindingSet(const std::string& name);
    [[nodiscard]] TopLevelAS* getTopLevelAccelerationStructure(const std::string& name);

    [[nodiscard]] const std::unordered_set<NodeDependency>& nodeDependencies() const;

private:
    Backend& m_backend;
    Backend& backend() { return m_backend; }

    Registry* m_previousRegistry { nullptr };

    std::optional<std::string> m_currentNodeName;
    std::unordered_set<NodeDependency> m_nodeDependencies;
    std::vector<std::string> m_allNodeNames;

    const RenderTarget& m_windowRenderTarget;

    template<typename ResourceType>
    struct PublishedResource {
        ResourceType* resource;
        std::string publisher;
    };

    template<typename ResourceType>
    using PublishedResourceMap = std::unordered_map<std::string, PublishedResource<ResourceType>>;

    PublishedResourceMap<Buffer> m_publishedBuffers;
    PublishedResourceMap<Texture> m_publishedTextures;
    PublishedResourceMap<BindingSet> m_publishedBindingSets;
    PublishedResourceMap<TopLevelAS> m_publishedTopLevelAS;

    template<typename T>
    void publishResource(const std::string& name, T& resource, PublishedResourceMap<T>& map);

    template<typename T>
    T* getResource(const std::string& name, const PublishedResourceMap<T>& map);

    std::vector<std::unique_ptr<Buffer>> m_buffers;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<std::unique_ptr<RenderTarget>> m_renderTargets;
    std::vector<std::unique_ptr<BindingSet>> m_bindingSets;
    std::vector<std::unique_ptr<RenderState>> m_renderStates;
    std::vector<std::unique_ptr<BottomLevelAS>> m_bottomLevelAS;
    std::vector<std::unique_ptr<TopLevelAS>> m_topLevelAS;
    std::vector<std::unique_ptr<RayTracingState>> m_rayTracingStates;
    std::vector<std::unique_ptr<ComputeState>> m_computeStates;
    std::vector<std::unique_ptr<UpscalingState>> m_upscalingStates;

    static constexpr size_t PersistentBufferSize = 10 * ark::conversion::constants::BytesToKilobytes;
    BumpAllocator m_persistentBuffer { PersistentBufferSize };
};

template<typename T>
[[nodiscard]] Buffer& Registry::createBuffer(const std::vector<T>& inData, Buffer::Usage usage)
{
    size_t dataSize = inData.size() * sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(inData.data());
    return createBuffer(binaryData, dataSize, usage);
}

template<typename T>
[[nodiscard]] Buffer& Registry::createBufferForData(const T& inData, Buffer::Usage usage)
{
    constexpr size_t dataSize = sizeof(T);
    auto* binaryData = reinterpret_cast<const std::byte*>(&inData);
    return createBuffer(binaryData, dataSize, usage);
}

template<typename T, typename... Args>
[[nodiscard]] T& Registry::allocate(Args&&... args)
{
    if (T* data = m_persistentBuffer.allocateAligned<T>()) {

        new (data) T(std::forward<Args>(args)...);
        return *data; // return as reference type

    } else {
        ARKOSE_LOG_FATAL("Registry ran out of persistent storage space while trying to allocate {} bytes. "
                         "This shouldn't fail, we probably want to increase the buffer size (current size {})",
                         sizeof(T), PersistentBufferSize);
    }
}

template<typename T>
void Registry::publishResource(const std::string& name, T& resource, std::unordered_map<std::string, PublishedResource<T>>& map)
{
    ARKOSE_ASSERT(name.length() > 0);
    ARKOSE_ASSERT(m_currentNodeName.has_value());
    auto nodeName = m_currentNodeName.value();

    if (resource.owningRegistry({}) != this) {
        ARKOSE_LOG(Fatal, "Registry: Attempt to publish the resource '{}' in node '{}', but the resource is not owned by this registry. "
                          "This could be caused by a per-node resource being published as a per-frame node, or similar.", name, nodeName);
    }

    if (map.find(name) != map.end()) {
        ARKOSE_LOG(Fatal, "Registry: Attempt to publish the resource '{}' in node '{}', but a resource of that name (and type) "
                          "has already been published. This is not valid, all resource must have unique names.", name, nodeName);
    }

    map[name] = PublishedResource<T> { .resource = &resource,
                                       .publisher = nodeName };

    //if (!resource.name().empty())
    //    ARKOSE_LOG(Info, "Renaming during publishing: '{}' -> '{}'", resource.name(), name);

    // Also set debug name for resource to the same name when publishing
    resource.setName(name);
}

template<typename T>
T* Registry::getResource(const std::string& name, const PublishedResourceMap<T>& map)
{
    if (!m_currentNodeName.has_value()) {
        ARKOSE_LOG(Fatal, "Registry: Attempt to get resource while not in the render pipline construct phase, which is illegal. "
                          "Any resources that you want to use in the execution phase you must first get in the construction phase.");
    }

    auto entry = map.find(name);
    if (entry == map.end())
        return nullptr;

    const PublishedResource<T>& publishedResource = entry->second;

    // Insert node dependency link
    // TODO: Later when we allow consuming and passing on resources this might be a good place to handle that!
    NodeDependency dependency { m_currentNodeName.value(), publishedResource.publisher };
    m_nodeDependencies.insert(dependency);

    return publishedResource.resource;
}

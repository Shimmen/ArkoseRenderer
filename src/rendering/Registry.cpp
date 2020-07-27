#include "Registry.h"

#include "utility/FileIO.h"
#include "utility/Image.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <stb_image.h>

Registry::Registry(Backend& backend, const RenderTarget* windowRenderTarget)
    : m_backend(backend)
    , m_windowRenderTarget(windowRenderTarget)
{
}

void Registry::setCurrentNode(std::string node)
{
    m_currentNodeName = std::move(node);
}

const RenderTarget& Registry::windowRenderTarget()
{
    if (!m_windowRenderTarget)
        LogErrorAndExit("Can't get the window render target from a non-frame registry!\n");
    return *m_windowRenderTarget;
}

RenderTarget& Registry::createRenderTarget(std::vector<RenderTarget::Attachment> attachments)
{
    auto renderTarget = backend().createRenderTarget(attachments);
    m_renderTargets.push_back(std::move(renderTarget));
    return *m_renderTargets.back();
}

Texture& Registry::createTexture2D(Extent2D extent, Texture::Format format, Texture::Multisampling ms)
{
    auto texture = backend().createTexture(extent, format, Texture::MinFilter::Linear, Texture::MagFilter::Linear, Texture::Mipmap::None, ms);
    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Buffer& Registry::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    if (size == 0)
        LogWarning("Warning: creating buffer of size 0!\n");
    auto buffer = backend().createBuffer(size, usage, memoryHint);
    m_buffers.push_back(std::move(buffer));
    return *m_buffers.back();
}

Buffer& Registry::createBuffer(const std::byte* data, size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    Buffer& buffer = createBuffer(size, usage, memoryHint);
    buffer.updateData(data, size);
    return buffer;
}

BindingSet& Registry::createBindingSet(std::vector<ShaderBinding> shaderBindings)
{
    auto bindingSet = backend().createBindingSet(shaderBindings);
    m_bindingSets.push_back(std::move(bindingSet));
    return *m_bindingSets.back();
}

Texture& Registry::createPixelTexture(vec4 pixelValue, bool srgb)
{
    auto format = srgb
        ? Texture::Format::sRGBA8
        : Texture::Format::RGBA8;

    auto texture = backend().createTexture({ 1, 1 }, format, Texture::MinFilter::Nearest, Texture::MagFilter::Nearest, Texture::Mipmap::None, Texture::Multisampling::None);
    texture->setPixelData(pixelValue);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps)
{
    // FIXME (maybe): Add async functionality though the Registry (i.e., every new frame it checks for new data and sees if it may update some)

    Image::Info* info = Image::getInfo(imagePath);
    if (!info)
        LogErrorAndExit("Registry: could not read image '%s', exiting\n", imagePath.c_str());

    Texture::Format format;
    Image::PixelType pixelTypeToUse;

    switch (info->pixelType) {
    case Image::PixelType::RGB:
    case Image::PixelType::RGBA:
        // Honestly, this is easier to read than the if-based equivalent..
        format = (info->isHdr())
            ? Texture::Format::RGBA32F
            : (srgb)
                ? Texture::Format::sRGBA8
                : Texture::Format::RGBA8;
        // RGB formats aren't always supported, so always use RGBA for 3-component data
        pixelTypeToUse = Image::PixelType::RGBA;
        break;
    default:
        LogErrorAndExit("Registry: currently no support for other than (s)RGB(F) and (s)RGBA(F) texture loading!\n");
    }

    auto mipmapMode = generateMipmaps && info->width > 1 && info->height > 1 ? Texture::Mipmap::Linear : Texture::Mipmap::None;
    auto texture = backend().createTexture({ info->width, info->height }, format, Texture::MinFilter::Linear, Texture::MagFilter::Linear, mipmapMode, Texture::Multisampling::None);

    Image* image = Image::load(imagePath, pixelTypeToUse);
    texture->setData(image->data(), image->size());

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

RenderState& Registry::createRenderState(const RenderStateBuilder& builder)
{
    return createRenderState(builder.renderTarget, builder.vertexLayout, builder.shader,
                             builder.bindingSets(), builder.viewport(), builder.blendState(), builder.rasterState(), builder.depthState());
}

RenderState& Registry::createRenderState(
    const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
    const Shader& shader, std::vector<const BindingSet*> bindingSets,
    const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState)
{
    auto renderState = backend().createRenderState(renderTarget, vertexLayout, shader, bindingSets, viewport, blendState, rasterState, depthState);
    m_renderStates.push_back(std::move(renderState));
    return *m_renderStates.back();
}

BottomLevelAS& Registry::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    auto blas = backend().createBottomLevelAccelerationStructure(geometries);
    m_bottomLevelAS.push_back(std::move(blas));
    return *m_bottomLevelAS.back();
}

TopLevelAS& Registry::createTopLevelAccelerationStructure(std::vector<RTGeometryInstance> instances)
{
    auto tlas = backend().createTopLevelAccelerationStructure(instances);
    m_topLevelAS.push_back(std::move(tlas));
    return *m_topLevelAS.back();
}

RayTracingState& Registry::createRayTracingState(const ShaderBindingTable& sbt, std::vector<const BindingSet*> bindingSets, uint32_t maxRecursionDepth)
{
    auto rtState = backend().createRayTracingState(sbt, bindingSets, maxRecursionDepth);
    m_rayTracingStates.push_back(std::move(rtState));
    return *m_rayTracingStates.back();
}

ComputeState& Registry::createComputeState(const Shader& shader, std::vector<const BindingSet*> bindingSets)
{
    auto computeState = backend().createComputeState(shader, bindingSets);
    m_computeStates.push_back(std::move(computeState));
    return *m_computeStates.back();
}

void Registry::publish(const std::string& name, Buffer& buffer)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameBufferMap.find(fullName);
    ASSERT(entry == m_nameBufferMap.end());
    m_nameBufferMap[fullName] = &buffer;
}

void Registry::publish(const std::string& name, Texture& texture)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameTextureMap.find(fullName);
    ASSERT(entry == m_nameTextureMap.end());
    m_nameTextureMap[fullName] = &texture;
}

void Registry::publish(const std::string& name, TopLevelAS& tlas)
{
    ASSERT(m_currentNodeName.has_value());
    std::string fullName = makeQualifiedName(m_currentNodeName.value(), name);
    auto entry = m_nameTopLevelASMap.find(fullName);
    ASSERT(entry == m_nameTopLevelASMap.end());
    m_nameTopLevelASMap[fullName] = &tlas;
}

std::optional<Texture*> Registry::getTexture(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameTextureMap.find(fullName);

    if (entry == m_nameTextureMap.end()) {
        return {};
    }

    ASSERT(m_currentNodeName.has_value());
    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    Texture* texture = entry->second;
    return texture;
}

Buffer* Registry::getBuffer(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameBufferMap.find(fullName);

    if (entry == m_nameBufferMap.end()) {
        return nullptr;
    }

    ASSERT(m_currentNodeName.has_value());
    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    Buffer* buffer = entry->second;
    return buffer;
}

TopLevelAS* Registry::getTopLevelAccelerationStructure(const std::string& renderPass, const std::string& name)
{
    std::string fullName = makeQualifiedName(renderPass, name);
    auto entry = m_nameTopLevelASMap.find(fullName);

    if (entry == m_nameTopLevelASMap.end()) {
        return nullptr;
    }

    ASSERT(m_currentNodeName.has_value());
    NodeDependency dependency { m_currentNodeName.value(), renderPass };
    m_nodeDependencies.insert(dependency);

    TopLevelAS* tlas = entry->second;
    return tlas;
}

const std::unordered_set<NodeDependency>& Registry::nodeDependencies() const
{
    return m_nodeDependencies;
}

Badge<Registry> Registry::exchangeBadges(Badge<Backend>) const
{
    return {};
}

std::string Registry::makeQualifiedName(const std::string& node, const std::string& name)
{
    return node + ':' + name;
}

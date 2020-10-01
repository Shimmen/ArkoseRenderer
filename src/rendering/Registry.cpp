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
    renderTarget->setOwningRegistry({}, this);

    m_renderTargets.push_back(std::move(renderTarget));
    return *m_renderTargets.back();
}

Texture& Registry::createTexture2D(Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, Texture::WrapModes wrapMode)
{
    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(extent, 1),
        .format = format,
        .minFilter = filters.min,
        .magFilter = filters.mag,
        .wrapMode = wrapMode,
        .mipmap = mipmap,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::createTextureArray(uint32_t itemCount, Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, Texture::WrapModes wrapMode)
{
    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = itemCount,
        .extent = Extent3D(extent, 1),
        .format = format,
        .minFilter = filters.min,
        .magFilter = filters.mag,
        .wrapMode = wrapMode,
        .mipmap = mipmap,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::createTextureFromImage(const Image& image, bool srgb, bool generateMipmaps, Texture::WrapModes wrapMode)
{
    auto mipmapMode = (generateMipmaps && image.info().width > 1 && image.info().height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::Format format;
    int numDesiredComponents;
    int pixelSizeBytes;

    switch (image.info().pixelType) {
    case Image::PixelType::RGB:
    case Image::PixelType::RGBA:
        numDesiredComponents = 4;
        if (image.info().isHdr()) {
            format = Texture::Format::RGBA32F;
            pixelSizeBytes = 4 * sizeof(float);
        } else {
            format = (srgb)
                ? Texture::Format::sRGBA8
                : Texture::Format::RGBA8;
            pixelSizeBytes = 4 * sizeof(uint8_t);
        }
        break;
    default:
        LogErrorAndExit("Registry: currently no support for other than (s)RGB(F) and (s)RGBA(F) texture loading (from image)!\n");
    }

    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = { (uint32_t)image.info().width, (uint32_t)image.info().height, 1 },
        .format = format,
        .minFilter = Texture::MinFilter::Linear,
        .magFilter = Texture::MagFilter::Linear,
        .wrapMode = wrapMode,
        .mipmap = mipmapMode,
        .multisampling = Texture::Multisampling::None
    };

    int width, height;
    void* rawPixelData;
    if (image.info().isHdr())
        rawPixelData = (void*)stbi_loadf_from_memory((const stbi_uc*)image.data(), image.size(), &width, &height, nullptr, numDesiredComponents);
    else
        rawPixelData = (void*)stbi_load_from_memory((const stbi_uc*)image.data(), image.size(), &width, &height, nullptr, numDesiredComponents);
    ASSERT(width == image.info().width);
    ASSERT(height == image.info().height);

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    uint32_t rawDataSize = width * height * pixelSizeBytes;
    texture->setData(rawPixelData, rawDataSize);
    stbi_image_free(rawPixelData);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::createMultisampledTexture2D(Extent2D extent, Texture::Format format, Texture::Multisampling multisampling, Texture::Mipmap mipmap)
{
    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(extent, 1),
        .format = format,
        .minFilter = Texture::MinFilter::Linear,
        .magFilter = Texture::MagFilter::Linear,
        .wrapMode = {
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat },
        .mipmap = mipmap,
        .multisampling = multisampling
    };

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::createCubemapTexture(Extent2D extent, Texture::Format format)
{
    Texture::TextureDescription desc {
        .type = Texture::Type::Cubemap,
        .arrayCount = 1u,
        .extent = Extent3D(extent, 1),
        .format = format,
        .minFilter = Texture::MinFilter::Linear,
        .magFilter = Texture::MagFilter::Linear,
        .wrapMode = {
            Texture::WrapMode::ClampToEdge,
            Texture::WrapMode::ClampToEdge,
            Texture::WrapMode::ClampToEdge },
        .mipmap = Texture::Mipmap::None,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Buffer& Registry::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    if (size == 0)
        LogWarning("Warning: creating buffer of size 0!\n");

    auto buffer = backend().createBuffer(size, usage, memoryHint);
    buffer->setOwningRegistry({}, this);

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
    bindingSet->setOwningRegistry({}, this);

    m_bindingSets.push_back(std::move(bindingSet));
    return *m_bindingSets.back();
}

Texture& Registry::createPixelTexture(vec4 pixelValue, bool srgb)
{
    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(1, 1, 1),
        .format = srgb
            ? Texture::Format::sRGBA8
            : Texture::Format::RGBA8,
        .minFilter = Texture::MinFilter::Nearest,
        .magFilter = Texture::MagFilter::Nearest,
        .wrapMode = {
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat },
        .mipmap = Texture::Mipmap::None,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

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

    auto mipmapMode = (generateMipmaps && info->width > 1 && info->height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = { (uint32_t)info->width, (uint32_t)info->height, 1 },
        .format = format,
        .minFilter = Texture::MinFilter::Linear,
        .magFilter = Texture::MagFilter::Linear,
        .wrapMode = {
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat,
            Texture::WrapMode::Repeat },
        .mipmap = mipmapMode,
        .multisampling = Texture::Multisampling::None
    };

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

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
    const Shader& shader, std::vector<BindingSet*> bindingSets,
    const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState)
{
    auto renderState = backend().createRenderState(renderTarget, vertexLayout, shader, bindingSets, viewport, blendState, rasterState, depthState);
    renderState->setOwningRegistry({}, this);

    m_renderStates.push_back(std::move(renderState));
    return *m_renderStates.back();
}

BottomLevelAS& Registry::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    auto blas = backend().createBottomLevelAccelerationStructure(geometries);
    blas->setOwningRegistry({}, this);

    m_bottomLevelAS.push_back(std::move(blas));
    return *m_bottomLevelAS.back();
}

TopLevelAS& Registry::createTopLevelAccelerationStructure(std::vector<RTGeometryInstance> instances)
{
    auto tlas = backend().createTopLevelAccelerationStructure(instances);
    tlas->setOwningRegistry({}, this);

    m_topLevelAS.push_back(std::move(tlas));
    return *m_topLevelAS.back();
}

RayTracingState& Registry::createRayTracingState(ShaderBindingTable& sbt, std::vector<BindingSet*> bindingSets, uint32_t maxRecursionDepth)
{
    auto rtState = backend().createRayTracingState(sbt, bindingSets, maxRecursionDepth);
    rtState->setOwningRegistry({}, this);

    m_rayTracingStates.push_back(std::move(rtState));
    return *m_rayTracingStates.back();
}

ComputeState& Registry::createComputeState(const Shader& shader, std::vector<BindingSet*> bindingSets)
{
    auto computeState = backend().createComputeState(shader, bindingSets);
    computeState->setOwningRegistry({}, this);

    m_computeStates.push_back(std::move(computeState));
    return *m_computeStates.back();
}

void Registry::publish(const std::string& name, Buffer& buffer)
{
    publishResource(name, buffer, m_nameBufferMap);
}

void Registry::publish(const std::string& name, Texture& texture)
{
    publishResource(name, texture, m_nameTextureMap);
}

void Registry::publish(const std::string& name, BindingSet& bindingSet)
{
    publishResource(name, bindingSet, m_nameBindingSetMap);
}

void Registry::publish(const std::string& name, TopLevelAS& tlas)
{
    publishResource(name, tlas, m_nameTopLevelASMap);
}

std::optional<Texture*> Registry::getTexture(const std::string& node, const std::string& name)
{
    Texture* texture = getResource(node, name, m_nameTextureMap);
    if (!texture)
        return {};
    return texture;
}

Buffer* Registry::getBuffer(const std::string& node, const std::string& name)
{
    return getResource(node, name, m_nameBufferMap);
}

BindingSet* Registry::getBindingSet(const std::string& node, const std::string& name)
{
    return getResource(node, name, m_nameBindingSetMap);
}

TopLevelAS* Registry::getTopLevelAccelerationStructure(const std::string& node, const std::string& name)
{
    return getResource(node, name, m_nameTopLevelASMap);
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

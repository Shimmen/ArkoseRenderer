#include "Registry.h"

#include "utility/FileIO.h"
#include "utility/Image.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <fmt/format.h>
#include <stb_image.h>

Registry::Registry(Backend& backend, Registry* previousRegistry, const RenderTarget* windowRenderTarget)
    : m_backend(backend)
    , m_previousRegistry(previousRegistry)
    , m_windowRenderTarget(windowRenderTarget)
{
    /*
    if (m_previousRegistry) {
        m_uploadBuffer = std::move(previousRegistry->m_uploadBuffer);
        m_uploadBuffer->reset();
    } else {
        static constexpr size_t registryUploadBufferSize = 4 * 1024 * 1024;
        m_uploadBuffer = std::make_unique<UploadBuffer>(backend, registryUploadBufferSize);
    }
    */
}

void Registry::newFrame(Badge<Backend>)
{
    //m_uploadBuffer->reset();
}

void Registry::setCurrentNode(const std::string& node)
{
    m_currentNodeName = node;
    m_allNodeNames.push_back(node);
}

const RenderTarget& Registry::windowRenderTarget()
{
    if (!m_windowRenderTarget)
        LogErrorAndExit("Can't get the window render target from a non-frame registry!\n");
    return *m_windowRenderTarget;
}

void Registry::setUploadBuffer(Badge<Backend>, UploadBuffer* uploadBuffer)
{
    m_uploadBuffer = uploadBuffer;
}

UploadBuffer& Registry::getUploadBuffer()
{
    ASSERT(m_uploadBuffer);
    return *m_uploadBuffer;
}

RenderTarget& Registry::createRenderTarget(std::vector<RenderTarget::Attachment> attachments)
{
    auto renderTarget = backend().createRenderTarget(attachments);
    renderTarget->setOwningRegistry({}, this);

    m_renderTargets.push_back(std::move(renderTarget));
    return *m_renderTargets.back();
}

static void validateTextureDescription(Texture::TextureDescription desc)
{
    // TODO: Add more validation
    if (desc.extent.width() == 0 || desc.extent.height() == 0 || desc.extent.depth() == 0)
        LogErrorAndExit("Registry: One or more texture dimensions are zero (extent={%u, %u, %u})\n", desc.extent.width(), desc.extent.height(), desc.extent.depth());
    if (desc.arrayCount == 0)
        LogErrorAndExit("Registry: Texture array count must be >= 1 but is %u\n", desc.arrayCount);
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

    validateTextureDescription(desc);
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

    validateTextureDescription(desc);
    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::createTextureFromImage(const Image& image, bool srgb, bool generateMipmaps, Texture::WrapModes wrapMode)
{
    SCOPED_PROFILE_ZONE()

    auto mipmapMode = (generateMipmaps && image.info().width > 1 && image.info().height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::Format format;
    int numDesiredComponents;
    int pixelSizeBytes;

    switch (image.info().pixelType) {
    case Image::PixelType::Grayscale:
        numDesiredComponents = 1;
        if (!srgb && image.info().isHdr()) {
            format = Texture::Format::R32F;
            pixelSizeBytes = sizeof(float);
        } else {
            LogErrorAndExit("Registry: no support for grayscale non-HDR or sRGB texture loading (from image)!\n");
        }
        break;
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
        LogErrorAndExit("Registry: currently no support for other than R32F, (s)RGB(F), and (s)RGBA(F) texture loading (from image)!\n");
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

    validateTextureDescription(desc);
    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    int width, height;
    const void* rawPixelData;
    switch (image.dataOwner()) {
    case Image::DataOwner::StbImage:
        if (image.info().isHdr())
            rawPixelData = (void*)stbi_loadf_from_memory((const stbi_uc*)image.data(), (int)image.size(), &width, &height, nullptr, numDesiredComponents);
        else
            rawPixelData = (void*)stbi_load_from_memory((const stbi_uc*)image.data(), (int)image.size(), &width, &height, nullptr, numDesiredComponents);
        ASSERT(width == image.info().width);
        ASSERT(height == image.info().height);
        break;
    case Image::DataOwner::External:
        rawPixelData = image.data();
        width = image.info().width;
        height = image.info().height;
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    uint32_t rawDataSize = width * height * pixelSizeBytes;
    texture->setData(rawPixelData, rawDataSize);

    if (image.dataOwner() == Image::DataOwner::StbImage)
        stbi_image_free(const_cast<void*>(rawPixelData));

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

    validateTextureDescription(desc);
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

std::pair<Texture&, Registry::ReuseMode> Registry::createOrReuseTexture2D(const std::string& name, Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, Texture::WrapModes wrapMode)
{
    if (m_previousRegistry) {
        for (std::unique_ptr<Texture>& oldTexture : m_previousRegistry->m_textures) {
            if (oldTexture && oldTexture->reusable({}) && oldTexture->name() == name) {

                // Verify that all parameters are the same (for reuse we obviouly need that it's the same between occations)
                ASSERT(extent == oldTexture->extent());
                ASSERT(format == oldTexture->format());
                ASSERT(filters.min == oldTexture->minFilter());
                ASSERT(filters.mag == oldTexture->magFilter());
                ASSERT(mipmap == oldTexture->mipmap());
                ASSERT(wrapMode.u == oldTexture->wrapMode().u);
                ASSERT(wrapMode.v == oldTexture->wrapMode().v);
                ASSERT(wrapMode.w == oldTexture->wrapMode().w);

                // Adopt the reused resource (m_previousRegistry will be destroyed so it's fine to move things from it)
                oldTexture->setOwningRegistry({}, this);
                m_textures.push_back(std::move(oldTexture));

                return { *m_textures.back(), ReuseMode::Reused };
            }
        }
    }

    Texture& texture = createTexture2D(extent, format, filters, mipmap, wrapMode);
    texture.setReusable({}, true);
    texture.setName(name);

    return { texture, ReuseMode::Created };
}

Texture& Registry::createOrReuseTextureArray(const std::string& name, uint32_t itemCount, Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, Texture::WrapModes wrapMode)
{
    if (m_previousRegistry) {
        for (std::unique_ptr<Texture>& oldTexture : m_previousRegistry->m_textures) {
            if (oldTexture && oldTexture->reusable({}) && oldTexture->name() == name) {

                // Verify that all parameters are the same (for reuse we obviouly need that it's the same between occations)
                ASSERT(itemCount == oldTexture->arrayCount());
                ASSERT(extent == oldTexture->extent());
                ASSERT(format == oldTexture->format());
                ASSERT(filters.min == oldTexture->minFilter());
                ASSERT(filters.mag == oldTexture->magFilter());
                ASSERT(mipmap == oldTexture->mipmap());
                ASSERT(wrapMode.u == oldTexture->wrapMode().u);
                ASSERT(wrapMode.v == oldTexture->wrapMode().v);
                ASSERT(wrapMode.w == oldTexture->wrapMode().w);

                // Adopt the reused resource (m_previousRegistry will be destroyed so it's fine to move things from it)
                oldTexture->setOwningRegistry({}, this);
                m_textures.push_back(std::move(oldTexture));

                return *m_textures.back();
            }
        }
    }

    Texture& texture = createTextureArray(itemCount, extent, format, filters, mipmap, wrapMode);
    texture.setReusable({}, true);
    texture.setName(name);

    return texture;
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

static void pixelFormatAndTypeForImageInfo(const Image::Info& info, bool sRGB, Texture::Format& format, Image::PixelType& pixelTypeToUse)
{
    switch (info.pixelType) {
    case Image::PixelType::RGB:
    case Image::PixelType::RGBA:
        // Honestly, this is easier to read than the if-based equivalent..
        format = (info.isHdr())
            ? Texture::Format::RGBA32F
            : (sRGB)
                ? Texture::Format::sRGBA8
                : Texture::Format::RGBA8;
        // RGB formats aren't always supported, so always use RGBA for 3-component data
        pixelTypeToUse = Image::PixelType::RGBA;
        break;
    default:
        LogErrorAndExit("Registry: currently no support for other than (s)RGB(F) and (s)RGBA(F) texture loading!\n");
    }
}

Texture& Registry::loadTexture2D(const std::string& imagePath, bool srgb, bool generateMipmaps)
{
    SCOPED_PROFILE_ZONE()

    // FIXME (maybe): Add async functionality though the Registry (i.e., every new frame it checks for new data and sees if it may update some)

    Image::Info* info = Image::getInfo(imagePath);
    if (!info)
        LogErrorAndExit("Registry: could not read image '%s', exiting\n", imagePath.c_str());

    Texture::Format format;
    Image::PixelType pixelTypeToUse;
    pixelFormatAndTypeForImageInfo(*info, srgb, format, pixelTypeToUse);

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
    texture->setName("Texture:" + imagePath);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::loadTextureArrayFromFileSequence(const std::string& imagePathPattern, bool srgb, bool generateMipmaps)
{
    SCOPED_PROFILE_ZONE()

    // FIXME (maybe): Add async functionality though the Registry (i.e., every new frame it checks for new data and sees if it may update some)

    std::vector<std::string> imagePaths;
    std::vector<Image::Info*> imageInfos;
    for (size_t idx = 0;; ++idx) {
        std::string imagePath = fmt::format(imagePathPattern, idx);
        Image::Info* imageInfo = Image::getInfo(imagePath, true);
        if (!imageInfo)
            break;
        imageInfos.push_back(imageInfo);
        imagePaths.push_back(imagePath);
    }

    if (imageInfos.size() == 0)
        LogErrorAndExit("Registry: could not find any images in image array pattern <%s>, exiting\n", imagePathPattern.c_str());

    // Use the first one as "prototype" image info
    Image::Info& info = *imageInfos.front();
    uint32_t arrayCount = static_cast<uint32_t>(imageInfos.size());

    // Ensure all are similar
    for (uint32_t idx = 1; idx < arrayCount; ++idx) {
        Image::Info& otherInfo = *imageInfos[idx];
        ASSERT(info == otherInfo);
    }

    Texture::Format format;
    Image::PixelType pixelTypeToUse;
    pixelFormatAndTypeForImageInfo(info, srgb, format, pixelTypeToUse);

    auto mipmapMode = (generateMipmaps && info.width > 1 && info.height > 1)
        ? Texture::Mipmap::Linear
        : Texture::Mipmap::None;

    Texture::TextureDescription desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = arrayCount,
        .extent = { (uint32_t)info.width, (uint32_t)info.height, 1 },
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
    texture->setName("Texture:" + imagePathPattern);

    // Set texture data
    {
        size_t totalSize = 0;
        std::vector<Image*> images {};
        images.reserve(imageInfos.size());
        for (const std::string& imagePath : imagePaths) {
            Image* image = Image::load(imagePath, pixelTypeToUse);
            images.push_back(image);
            totalSize += image->size();
        }

        // TODO: Maybe we can just map the individual image into memory directly?
        uint8_t* textureArrayMemory = static_cast<uint8_t*>(malloc(totalSize));
        AtScopeExit freeMemory { [&]() { free(textureArrayMemory); } };

        size_t cursor = 0;
        for (const Image* image : images) {
            std::memcpy(textureArrayMemory + cursor, image->data(), image->size());
            cursor += image->size();
        }
        ASSERT(cursor == totalSize);

        texture->setData(textureArrayMemory, totalSize);
    }

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

RenderState& Registry::createRenderState(const RenderStateBuilder& builder)
{
    return createRenderState(builder.renderTarget, builder.vertexLayout, builder.shader,
                             builder.stateBindings(), builder.viewport(), builder.blendState(), builder.rasterState(), builder.depthState(), builder.stencilState());
}

RenderState& Registry::createRenderState(
    const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
    const Shader& shader, const StateBindings& stateBindings,
    const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState, const StencilState& stencilState)
{
    auto renderState = backend().createRenderState(renderTarget, vertexLayout, shader, stateBindings, viewport, blendState, rasterState, depthState, stencilState);
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

RayTracingState& Registry::createRayTracingState(ShaderBindingTable& sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
{
    auto rtState = backend().createRayTracingState(sbt, stateBindings, maxRecursionDepth);
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

bool Registry::hasPreviousNode(const std::string& name) const
{
    auto entry = std::find(m_allNodeNames.begin(), m_allNodeNames.end(), name);
    return entry != m_allNodeNames.end();
}

void Registry::publish(const std::string& name, Buffer& buffer)
{
    publishResource(name, buffer, m_publishedBuffers);
}

void Registry::publish(const std::string& name, Texture& texture)
{
    publishResource(name, texture, m_publishedTextures);
}

void Registry::publish(const std::string& name, BindingSet& bindingSet)
{
    publishResource(name, bindingSet, m_publishedBindingSets);
}

void Registry::publish(const std::string& name, TopLevelAS& tlas)
{
    publishResource(name, tlas, m_publishedTopLevelAS);
}

Texture* Registry::getTexture(const std::string& name)
{
    return getResource(name, m_publishedTextures);
}

Buffer* Registry::getBuffer(const std::string& name)
{
    return getResource(name, m_publishedBuffers);
}

BindingSet* Registry::getBindingSet(const std::string& name)
{
    return getResource(name, m_publishedBindingSets);
}

TopLevelAS* Registry::getTopLevelAccelerationStructure(const std::string& name)
{
    return getResource(name, m_publishedTopLevelAS);
}

const std::unordered_set<NodeDependency>& Registry::nodeDependencies() const
{
    return m_nodeDependencies;
}

#include "Resources.h"

#include "backend/Backend.h"
#include "utility/Logging.h"
#include "utility/util.h"
#include <algorithm>

Resource::Resource()
    : m_backend(nullptr)
{
}

Resource::Resource(Backend& backend)
    : m_backend(&backend)
{
}

Resource::Resource(Resource&& other) noexcept
    : m_backend(other.m_backend)
{
    other.m_backend = nullptr;
}

const std::string& Resource::name() const
{
    return m_name;
}

void Resource::setName(const std::string& name)
{
    m_name = name;
}

void Resource::setReusable(Badge<Registry>, bool reusable)
{
    m_reusable = reusable;
}

void Resource::setOwningRegistry(Badge<Registry>, Registry* registry)
{
    m_owningRegistry = registry;
}

Resource& Resource::operator=(Resource&& other) noexcept
{
    m_backend = other.m_backend;
    other.m_backend = nullptr;
    return *this;
}

Texture::Texture(Backend& backend, TextureDescription desc)
    : Resource(backend)
    , m_type(desc.type)
    , m_arrayCount(desc.arrayCount)
    , m_extent(desc.extent)
    , m_format(desc.format)
    , m_minFilter(desc.minFilter)
    , m_magFilter(desc.magFilter)
    , m_wrapMode(desc.wrapMode)
    , m_mipmap(desc.mipmap)
    , m_multisampling(desc.multisampling)
{
    // (according to most specifications we can't have both multisampling and mipmapping)
    ASSERT(m_multisampling == Multisampling::None || m_mipmap == Mipmap::None);

    // At least one item in an implicit array
    ASSERT(m_arrayCount > 0);
}

bool Texture::hasFloatingPointDataFormat() const
{
    switch (format()) {
    case Texture::Format::R32:
    case Texture::Format::RGBA8:
    case Texture::Format::sRGBA8:
        return false;
    case Texture::Format::R16F:
    case Texture::Format::RGBA16F:
    case Texture::Format::RGBA32F:
    case Texture::Format::Depth32F:
        return true;
    case Texture::Format::Unknown:
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

bool Texture::hasMipmaps() const
{
    return m_mipmap != Mipmap::None;
}

uint32_t Texture::mipLevels() const
{
    if (hasMipmaps()) {
        uint32_t size = std::max(extent().width(), extent().height());
        uint32_t levels = static_cast<uint32_t>(std::floor(std::log2(size)) + 1);
        return levels;
    } else {
        return 1;
    }
}

bool Texture::isMultisampled() const
{
    return m_multisampling != Multisampling::None;
}

Texture::Multisampling Texture::multisampling() const
{
    return m_multisampling;
}

void forEachCubemapSide(std::function<void(CubemapSide, uint32_t)> callback)
{
    for (uint32_t idx = 0; idx < 6; ++idx) {
        auto side = static_cast<CubemapSide>(idx);
        callback(side, idx);
    }
}

RenderTarget::RenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : Resource(backend)
{
    // TODO: This is all very messy and could probably be cleaned up a fair bit!

    for (const Attachment& attachment : attachments) {
        if (attachment.type == AttachmentType::Depth) {
            ASSERT(!m_depthAttachment.has_value());
            m_depthAttachment = attachment;
        } else {
            m_colorAttachments.push_back(attachment);
        }
    }

    if (totalAttachmentCount() < 1) {
        LogErrorAndExit("RenderTarget error: tried to create with less than one attachments!\n");
    }

    for (auto& colorAttachment : m_colorAttachments) {
        if (!colorAttachment.texture->isMultisampled() && colorAttachment.multisampleResolveTexture != nullptr)
            LogErrorAndExit("RenderTarget error: tried to create render target with texture that isn't multisampled but has a resolve texture\n");
        if (colorAttachment.texture->isMultisampled() && colorAttachment.multisampleResolveTexture == nullptr)
            LogErrorAndExit("RenderTarget error: tried to create render target with multisample texture but no resolve texture\n");
    }

    Extent2D firstExtent = m_depthAttachment.has_value()
        ? m_depthAttachment.value().texture->extent()
        : m_colorAttachments.front().texture->extent();
    Texture::Multisampling firstMultisampling = m_depthAttachment.has_value()
        ? m_depthAttachment.value().texture->multisampling()
        : m_colorAttachments.front().texture->multisampling();

    for (auto& attachment : m_colorAttachments) {
        if (attachment.texture->extent() != firstExtent) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different sizes: (%ix%i) vs (%ix%i)\n",
                            attachment.texture->extent().width(), attachment.texture->extent().height(),
                            firstExtent.width(), firstExtent.height());
        }
        if (attachment.texture->multisampling() != firstMultisampling) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different multisampling sample counts: %u vs %u\n",
                            static_cast<unsigned>(attachment.texture->multisampling()), static_cast<unsigned>(firstMultisampling));
        }
    }

    m_extent = firstExtent;
    m_multisampling = firstMultisampling;

    if (colorAttachmentCount() == 0) {
        return;
    }

    // Keep color attachments sorted from Color0, Color1, .. ColorN
    std::sort(m_colorAttachments.begin(), m_colorAttachments.end(), [](const Attachment& left, const Attachment& right) {
        return left.type < right.type;
    });

    // Make sure we don't have duplicated attachment types & that the color attachments aren't sparse
    if (m_colorAttachments[0].type != AttachmentType::Color0)
        LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
    auto lastType = AttachmentType::Color0;
    for (size_t i = 1; i < m_colorAttachments.size(); ++i) {
        const Attachment& attachment = m_colorAttachments[i];
        if (attachment.type == lastType)
            LogErrorAndExit("RenderTarget error: duplicate attachment types in render target\n");
        if (static_cast<unsigned>(attachment.type) != static_cast<unsigned>(lastType) + 1)
            LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
        lastType = attachment.type;
    }
}

const Extent2D& RenderTarget::extent() const
{
    return m_extent;
}

size_t RenderTarget::colorAttachmentCount() const
{
    return m_colorAttachments.size();
}

size_t RenderTarget::totalAttachmentCount() const
{
    size_t total = colorAttachmentCount();
    if (hasDepthAttachment())
        total += 1;
    return total;
}

bool RenderTarget::hasDepthAttachment() const
{
    return m_depthAttachment.has_value();
}

const std::optional<RenderTarget::Attachment>& RenderTarget::depthAttachment() const
{
    return m_depthAttachment;
}

const std::vector<RenderTarget::Attachment>& RenderTarget::colorAttachments() const
{
    return m_colorAttachments;
}

Texture* RenderTarget::attachment(AttachmentType requestedType) const
{
    if (requestedType == AttachmentType::Depth) {
        if (m_depthAttachment.has_value())
            return m_depthAttachment.value().texture;
        return nullptr;
    }

    for (const RenderTarget::Attachment& attachment : m_colorAttachments) {
        if (attachment.type == requestedType)
            return attachment.texture;
    }

    return nullptr;
}

void RenderTarget::forEachAttachmentInOrder(std::function<void(const Attachment&)> callback) const
{
    for (auto& colorAttachment : m_colorAttachments) {
        callback(colorAttachment);
    }
    if (hasDepthAttachment())
        callback(depthAttachment().value());
}

bool RenderTarget::requiresMultisampling() const
{
    return m_multisampling != Texture::Multisampling::None;
}

Texture::Multisampling RenderTarget::multisampling() const
{
    return m_multisampling;
}

Buffer::Buffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Resource(backend)
    , m_size(size)
    , m_usage(usage)
    , m_memoryHint(memoryHint)
{
}

bool Buffer::updateDataAndGrowIfRequired(const std::byte* data, size_t size, size_t offset)
{
    size_t requiredBufferSize = offset + size;

    bool didGrow = false;
    if (m_size < requiredBufferSize) {
        size_t newSize = std::max(2 * m_size, requiredBufferSize);
        reallocateWithSize(newSize, Buffer::ReallocateStrategy::CopyExistingData);
        didGrow = true;
    }

    updateData(data, size, offset);
    return didGrow;
}

UploadBuffer::UploadBuffer(Backend& backend, size_t size)
{
    // TODO: Maybe create a persistent mapping for this buffer? Makes sense considering its use.
    m_buffer = backend.createBuffer(size, Buffer::Usage::Transfer, Buffer::MemoryHint::TransferOptimal);
}

void UploadBuffer::reset()
{
    m_cursor = 0;
    m_pendingOperations.clear();
}

BufferCopyOperation UploadBuffer::upload(const void* data, size_t size, Buffer& dstBuffer)
{
    size_t requiredSize = m_cursor + size;
    if (requiredSize > m_buffer->size())
        LogWarning("UploadBuffer: needs to grow to fit all requested uploads! It might be good to increase the default size so we don't have to pay this runtime cost");

    BufferCopyOperation copyOperation;
    copyOperation.size = size;

    copyOperation.srcBuffer = m_buffer.get();
    copyOperation.srcOffset = m_cursor;

    copyOperation.dstBuffer = &dstBuffer;
    copyOperation.dstOffset = 0;

    m_buffer->updateDataAndGrowIfRequired(data, size, m_cursor);
    m_cursor += size;

    m_pendingOperations.push_back(copyOperation);
    return copyOperation;
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, Buffer* buffer)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , tlas(nullptr)
    , buffers({ buffer })
    , textures()
{
    if (!buffer) {
        LogErrorAndExit("ShaderBinding error: null buffer\n");
    }

    switch (buffer->usage()) {
    case Buffer::Usage::UniformBuffer:
        type = ShaderBindingType::UniformBuffer;
        break;
    case Buffer::Usage::Vertex: // includes storage buffer support
    case Buffer::Usage::Index: // includes storage buffer support
    case Buffer::Usage::StorageBuffer:
    case Buffer::Usage::IndirectBuffer:
        type = ShaderBindingType::StorageBuffer;
        break;
    default:
        LogErrorAndExit("ShaderBinding error: invalid buffer for shader binding (not index or uniform buffer)\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, Texture* texture, ShaderBindingType type)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , type(type)
    , tlas(nullptr)
    , buffers()
    , textures({ texture })
{
    if (!texture) {
        LogErrorAndExit("ShaderBinding error: null texture\n");
    }

    if (type == ShaderBindingType::StorageImage && (texture->hasSrgbFormat() || texture->hasDepthFormat())) {
        LogErrorAndExit("ShaderBinding error: can't use texture with sRGB or depth format as storage image\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, TopLevelAS* tlas)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::RTAccelerationStructure)
    , tlas(tlas)
    , buffers()
    , textures()
{
    if (!tlas) {
        LogErrorAndExit("ShaderBinding error: null acceleration structure\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Texture*>& textures, uint32_t count)
    : bindingIndex(index)
    , count(count)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::TextureSamplerArray)
    , tlas(nullptr)
    , buffers()
    , textures(textures)
{
    if (count < textures.size()) {
        LogErrorAndExit("ShaderBinding error: too many textures in list\n");
    }

    for (auto texture : textures) {
        if (!texture) {
            LogErrorAndExit("ShaderBinding error: null texture in list\n");
        }
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Buffer*>& buffers)
    : bindingIndex(index)
    , count((uint32_t)buffers.size())
    , shaderStage(shaderStage)
    , type(ShaderBindingType::StorageBufferArray)
    , tlas(nullptr)
    , buffers(buffers)
    , textures()
{
    if (count < 1) {
        //LogErrorAndExit("ShaderBinding error: too few buffers in list\n");
    }

    for (auto buffer : buffers) {
        if (!buffer) {
            LogErrorAndExit("ShaderBinding error: null buffer in list\n");
        }
        if (buffer->usage() != Buffer::Usage::StorageBuffer && buffer->usage() != Buffer::Usage::IndirectBuffer) {
            LogErrorAndExit("ShaderBinding error: buffer in list is not a storage buffer\n");
        }
    }
}

BindingSet::BindingSet(Backend& backend, std::vector<ShaderBinding> shaderBindings)
    : Resource(backend)
    , m_shaderBindings(shaderBindings)
{
    std::sort(m_shaderBindings.begin(), m_shaderBindings.end(), [](const ShaderBinding& left, const ShaderBinding& right) {
        return left.bindingIndex < right.bindingIndex;
    });

    for (size_t i = 0; i < m_shaderBindings.size() - 1; ++i) {
        if (m_shaderBindings[i].bindingIndex == m_shaderBindings[i + 1].bindingIndex) {
            LogErrorAndExit("BindingSet error: duplicate bindings\n");
        }
    }
}

void StateBindings::at(uint32_t index, BindingSet& bindingSet)
{
    if (index >= (int64_t)m_orderedBindingSets.size()) {
        m_orderedBindingSets.resize(size_t(index) + 1);
    }

    ASSERT(m_orderedBindingSets[index] == nullptr);
    m_orderedBindingSets[index] = &bindingSet;
}

const std::vector<ShaderBinding>& BindingSet::shaderBindings() const
{
    return m_shaderBindings;
}

RenderStateBuilder::RenderStateBuilder(const RenderTarget& renderTarget, const Shader& shader, VertexLayout vertexLayout)
    : renderTarget(renderTarget)
    , shader(shader)
    , vertexLayout(vertexLayout)
{
}

Viewport RenderStateBuilder::viewport() const
{
    if (m_viewport.has_value()) {
        return m_viewport.value();
    }

    Viewport view {
        .x = 0.0f,
        .y = 0.0f,
        .extent = renderTarget.extent()
    };
    return view;
}

BlendState RenderStateBuilder::blendState() const
{
    if (m_blendState.has_value()) {
        return m_blendState.value();
    }

    BlendState state {
        .enabled = false
    };
    return state;
}

RasterState RenderStateBuilder::rasterState() const
{
    if (m_rasterState.has_value()) {
        return m_rasterState.value();
    }

    RasterState state {
        .backfaceCullingEnabled = cullBackfaces,
        .frontFace = frontFace,
        .polygonMode = polygonMode
    };
    return state;
}

DepthState RenderStateBuilder::depthState() const
{
    DepthState state {
        .writeDepth = writeDepth,
        .testDepth = testDepth,
        .compareOp = depthCompare,
    };
    return state;
}

StencilState RenderStateBuilder::stencilState() const
{
    StencilState state {
        .mode = stencilMode
    };
    return state;
}

RenderStateBuilder& RenderStateBuilder::addBindingSet(BindingSet& bindingSet)
{
    m_bindingSets.emplace_back(&bindingSet);
    return *this;
}

const std::vector<BindingSet*>& RenderStateBuilder::bindingSets() const
{
    return m_bindingSets;
}

RTGeometry::RTGeometry(RTTriangleGeometry triangles)
    : m_internal(triangles)
{
}

RTGeometry::RTGeometry(RTAABBGeometry aabbs)
    : m_internal(aabbs)
{
}

bool RTGeometry::hasTriangles() const
{
    return std::holds_alternative<RTTriangleGeometry>(m_internal);
}

bool RTGeometry::hasAABBs() const
{
    return std::holds_alternative<RTAABBGeometry>(m_internal);
}

const RTTriangleGeometry& RTGeometry::triangles() const
{
    return std::get<RTTriangleGeometry>(m_internal);
}

const RTAABBGeometry& RTGeometry::aabbs() const
{
    return std::get<RTAABBGeometry>(m_internal);
}

BottomLevelAS::BottomLevelAS(Backend& backend, std::vector<RTGeometry> geometries)
    : Resource(backend)
    , m_geometries(geometries)
{
}

const std::vector<RTGeometry>& BottomLevelAS::geometries() const
{
    return m_geometries;
}

TopLevelAS::TopLevelAS(Backend& backend, std::vector<RTGeometryInstance> instances)
    : Resource(backend)
    , m_instances(instances)
{
}

const std::vector<RTGeometryInstance>& TopLevelAS::instances() const
{
    return m_instances;
}

uint32_t TopLevelAS::instanceCount() const
{
    return static_cast<uint32_t>(m_instances.size());
}

RayTracingState::RayTracingState(Backend& backend, ShaderBindingTable sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
    : Resource(backend)
    , m_shaderBindingTable(sbt)
    , m_stateBindings(stateBindings)
    , m_maxRecursionDepth(maxRecursionDepth)
{
}

uint32_t RayTracingState::maxRecursionDepth() const
{
    return m_maxRecursionDepth;
}

const ShaderBindingTable& RayTracingState::shaderBindingTable() const
{
    return m_shaderBindingTable;
}

ComputeState::ComputeState(Backend& backend, Shader shader, std::vector<BindingSet*> bindingSets)
    : Resource(backend)
    , m_shader(shader)
    , m_bindingSets(bindingSets)
{
}

HitGroup::HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit, std::optional<ShaderFile> intersection)
    : m_closestHit(closestHit)
    , m_anyHit(anyHit)
    , m_intersection(intersection)
{
    ASSERT(closestHit.type() == ShaderFileType::RTClosestHit);
    ASSERT(!anyHit.has_value() || anyHit.value().type() == ShaderFileType::RTAnyHit);
    ASSERT(!intersection.has_value() || intersection.value().type() == ShaderFileType::RTIntersection);
}

ShaderBindingTable::ShaderBindingTable(ShaderFile rayGen, std::vector<HitGroup> hitGroups, std::vector<ShaderFile> missShaders)
    : m_rayGen(rayGen)
    , m_hitGroups(std::move(hitGroups))
    , m_missShaders(std::move(missShaders))
{
    ASSERT(m_rayGen.type() == ShaderFileType::RTRaygen);
    ASSERT(!m_hitGroups.empty());
    for (const auto& miss : m_missShaders) {
        ASSERT(miss.type() == ShaderFileType::RTMiss);
    }
}

std::vector<ShaderFile> ShaderBindingTable::allReferencedShaderFiles() const
{
    std::vector<ShaderFile> files;

    files.push_back(rayGen());

    for (const HitGroup& hitGroup : hitGroups()) {
        files.push_back(hitGroup.closestHit());
        if (hitGroup.hasAnyHitShader()) {
            files.push_back(hitGroup.anyHit());
        }
        if (hitGroup.hasIntersectionShader()) {
            files.push_back(hitGroup.intersection());
        }
    }

    for (const ShaderFile& missShader : missShaders()) {
        files.push_back(missShader);
    }

    return files;
}

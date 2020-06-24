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

Resource& Resource::operator=(Resource&& other) noexcept
{
    m_backend = other.m_backend;
    other.m_backend = nullptr;
    return *this;
}

Texture::Texture(Backend& backend, Extent2D extent, Format format, Usage usage, MinFilter minFilter, MagFilter magFilter, Mipmap mipmap, Multisampling multisampling)
    : Resource(backend)
    , m_extent(extent)
    , m_format(format)
    , m_usage(usage)
    , m_minFilter(minFilter)
    , m_magFilter(magFilter)
    , m_mipmap(mipmap)
    , m_multisampling(multisampling)
{
    // (according to most specifications we can't have both multisampling and mipmapping)
    ASSERT(multisampling == Multisampling::None || mipmap == Mipmap::None);
}

bool Texture::hasMipmaps() const
{
    return m_mipmap != Mipmap::None;
}

uint32_t Texture::mipLevels() const
{
    if (hasMipmaps()) {
        uint32_t size = std::max(extent().width(), extent().height());
        uint32_t levels = std::floor(std::log2(size)) + 1;
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

RenderTarget::RenderTarget(Backend& backend, std::vector<Attachment> attachments)
    : Resource(backend)
    , m_attachments {}
{
    // TODO: This is all very messy and could probably be cleaned up a fair bit!

    for (const Attachment& attachment : attachments) {
        m_attachments.emplace_back(attachment);
    }

    if (totalAttachmentCount() < 1) {
        LogErrorAndExit("RenderTarget error: tried to create with less than one attachments!\n");
    }

    for (auto& attachment : m_attachments) {
        const Texture& texture = *attachment.texture;
        if (texture.usage() != Texture::Usage::Attachment && texture.usage() != Texture::Usage::AttachAndSample) {
            LogErrorAndExit("RenderTarget error: tried to create with texture that can't be used as attachment\n");
        }
    }

    if (totalAttachmentCount() < 2) {
        return;
    }

    Extent2D firstExtent = m_attachments.front().texture->extent();

    for (auto& attachment : m_attachments) {
        if (attachment.texture->extent() != firstExtent) {
            LogErrorAndExit("RenderTarget error: tried to create with attachments of different sizes: (%ix%i) vs (%ix%i)\n",
                            attachment.texture->extent().width(), attachment.texture->extent().height(),
                            firstExtent.width(), firstExtent.height());
        }
    }

    // Keep attachments sorted from Color0, Color1, .. ColorN, Depth
    std::sort(m_attachments.begin(), m_attachments.end(), [](const Attachment& left, const Attachment& right) {
        return left.type < right.type;
    });

    // Make sure we don't have duplicated attachment types & that the color attachments aren't sparse
    if (m_attachments.front().type != AttachmentType::Depth && m_attachments.front().type != AttachmentType::Color0) {
        LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
    }
    std::optional<AttachmentType> lastType {};
    for (auto& attachment : m_attachments) {
        if (lastType.has_value()) {
            if (attachment.type == lastType.value()) {
                LogErrorAndExit("RenderTarget error: duplicate attachment types in render target\n");
            }
            if (attachment.type != AttachmentType::Depth) {
                auto lastVal = static_cast<unsigned int>(attachment.type);
                auto currVal = static_cast<unsigned int>(lastType.value());
                if (currVal != lastVal + 1) {
                    LogErrorAndExit("RenderTarget error: sparse color attachments in render target\n");
                }
            }
        }
    }
}

const Extent2D& RenderTarget::extent() const
{
    return m_attachments.front().texture->extent();
}

size_t RenderTarget::colorAttachmentCount() const
{
    size_t total = totalAttachmentCount();
    if (hasDepthAttachment()) {
        return total - 1;
    } else {
        return total;
    }
}

size_t RenderTarget::totalAttachmentCount() const
{
    return m_attachments.size();
}

bool RenderTarget::hasDepthAttachment() const
{
    if (m_attachments.empty()) {
        return false;
    }

    const Attachment& last = m_attachments.back();
    return last.type == AttachmentType::Depth;
}

const Texture* RenderTarget::attachment(AttachmentType requestedType) const
{
    for (const auto& [type, texture, _, __] : m_attachments) {
        if (type == requestedType) {
            return texture;
        }
    }
    return nullptr;
}

const std::vector<RenderTarget::Attachment>& RenderTarget::sortedAttachments() const
{
    return m_attachments;
}

void RenderTarget::forEachColorAttachment(std::function<void(const Attachment&)> callback) const
{
    for (const auto& attachment : m_attachments) {
        if (attachment.type != AttachmentType::Depth) {
            callback(attachment);
        }
    }
}

Buffer::Buffer(Backend& backend, size_t size, Usage usage, MemoryHint memoryHint)
    : Resource(backend)
    , m_size(size)
    , m_usage(usage)
    , m_memoryHint(memoryHint)
{
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const Buffer* buffer, ShaderBindingType type)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , type(type)
    , buffers({ buffer })
    , textures()
{
    if (!buffer) {
        LogErrorAndExit("ShaderBinding error: null buffer\n");
    }

    if (type != ShaderBindingType::UniformBuffer && type != ShaderBindingType::StorageBuffer) {
        LogErrorAndExit("ShaderBinding error: invalid shader binding type for buffer\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const Texture* texture, ShaderBindingType type)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , type(type)
    , buffers()
    , textures({ texture })
{
    if (!texture) {
        LogErrorAndExit("ShaderBinding error: null texture\n");
    }

    auto usage = texture->usage();

    switch (type) {
    case ShaderBindingType::TextureSampler:
        if (usage != Texture::Usage::Sampled && usage != Texture::Usage::AttachAndSample && usage != Texture::Usage::StorageAndSample) {
            LogErrorAndExit("ShaderBinding error: texture does not have a usage valid for being sampled\n");
        }
        break;
    case ShaderBindingType::StorageImage:
        if (usage != Texture::Usage::StorageAndSample) {
            LogErrorAndExit("ShaderBinding error: texture is not a storage image\n");
        }
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const TopLevelAS* tlas)
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

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<const Texture*>& textures, uint32_t count)
    : bindingIndex(index)
    , count(count)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::TextureSamplerArray)
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
        if (texture->usage() != Texture::Usage::Sampled && texture->usage() != Texture::Usage::AttachAndSample) {
            LogErrorAndExit("ShaderBinding error: texture in list does not support sampling\n");
        }
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<const Buffer*>& buffers)
    : bindingIndex(index)
    , count(buffers.size())
    , shaderStage(shaderStage)
    , type(ShaderBindingType::StorageBufferArray)
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
        if (buffer->usage() != Buffer::Usage::StorageBuffer) {
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

const std::vector<ShaderBinding>& BindingSet::shaderBindings() const
{
    return m_shaderBindings;
}

RenderStateBuilder::RenderStateBuilder(const RenderTarget& renderTarget, const Shader& shader, VertexLayout vertexLayout)
    : renderTarget(renderTarget)
    , vertexLayout(vertexLayout)
    , shader(shader)
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
        .backfaceCullingEnabled = true,
        .frontFace = TriangleWindingOrder::CounterClockwise,
        .polygonMode = polygonMode
    };
    return state;
}

DepthState RenderStateBuilder::depthState() const
{
    DepthState state {
        .writeDepth = writeDepth,
        .testDepth = testDepth,
    };
    return state;
}

RenderStateBuilder& RenderStateBuilder::addBindingSet(const BindingSet& bindingSet)
{
    m_bindingSets.emplace_back(&bindingSet);
    return *this;
}

const std::vector<const BindingSet*>& RenderStateBuilder::bindingSets() const
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
    return m_instances.size();
}

RayTracingState::RayTracingState(Backend& backend, ShaderBindingTable sbt, std::vector<const BindingSet*> bindingSets, uint32_t maxRecursionDepth)
    : Resource(backend)
    , m_shaderBindingTable(sbt)
    , m_bindingSets(bindingSets)
    , m_maxRecursionDepth(maxRecursionDepth)
{
}

uint32_t RayTracingState::maxRecursionDepth() const
{
    return m_maxRecursionDepth;
}

const const ShaderBindingTable& RayTracingState::shaderBindingTable() const
{
    return m_shaderBindingTable;
}

const std::vector<const BindingSet*>& RayTracingState::bindingSets() const
{
    return m_bindingSets;
}

ComputeState::ComputeState(Backend& backend, Shader shader, std::vector<const BindingSet*> bindingSets)
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

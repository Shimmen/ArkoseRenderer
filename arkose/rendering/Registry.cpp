#include "Registry.h"

#include "utility/FileIO.h"
#include "core/Logging.h"
#include "core/Assert.h"
#include <stb_image.h>

Registry::Registry(Backend& backend, const RenderTarget& windowRenderTarget, Registry* previousRegistry)
    : m_backend(backend)
    , m_previousRegistry(previousRegistry)
    , m_windowRenderTarget(windowRenderTarget)
{
}

void Registry::setCurrentNode(Badge<RenderPipeline>, std::optional<std::string> node)
{
    m_currentNodeName = node;
    if (node.has_value())
        m_allNodeNames.push_back(node.value());
}

const RenderTarget& Registry::windowRenderTarget()
{
    return m_windowRenderTarget;
}

RenderTarget& Registry::createRenderTarget(std::vector<RenderTarget::Attachment> attachments)
{
    auto renderTarget = backend().createRenderTarget(attachments);
    renderTarget->setOwningRegistry({}, this);

    m_renderTargets.push_back(std::move(renderTarget));
    return *m_renderTargets.back();
}

static void validateTextureDescription(Texture::Description desc)
{
    // TODO: Add more validation
    if (desc.extent.width() == 0 || desc.extent.height() == 0 || desc.extent.depth() == 0)
        ARKOSE_LOG(Fatal, "Registry: One or more texture dimensions are zero (extent={{{}, {}, {}}})", desc.extent.width(), desc.extent.height(), desc.extent.depth());
    if (desc.arrayCount == 0)
        ARKOSE_LOG(Fatal, "Registry: Texture array count must be >= 1 but is {}", desc.arrayCount);
}

Texture& Registry::createTexture(Texture::Description const& desc)
{
    validateTextureDescription(desc);

    auto texture = backend().createTexture(desc);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::createTexture2D(Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, ImageWrapModes wrapMode)
{
    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(extent, 1),
        .format = format,
        .filter = filters,
        .wrapMode = wrapMode,
        .mipmap = mipmap,
        .multisampling = Texture::Multisampling::None
    };

    return createTexture(desc);
}

Texture& Registry::createTextureArray(uint32_t itemCount, Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, ImageWrapModes wrapMode)
{
    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = itemCount,
        .extent = Extent3D(extent, 1),
        .format = format,
        .filter = filters,
        .wrapMode = wrapMode,
        .mipmap = mipmap,
        .multisampling = Texture::Multisampling::None
    };

    return createTexture(desc);
}

Texture& Registry::createMultisampledTexture2D(Extent2D extent, Texture::Format format, Texture::Multisampling multisampling, Texture::Mipmap mipmap)
{
    Texture::Description desc {
        .type = Texture::Type::Texture2D,
        .arrayCount = 1u,
        .extent = Extent3D(extent, 1),
        .format = format,
        .filter = Texture::Filters::linear(),
        .wrapMode = ImageWrapModes::repeatAll(),
        .mipmap = mipmap,
        .multisampling = multisampling
    };

    return createTexture(desc);
}

Texture& Registry::createCubemapTexture(Extent2D extent, Texture::Format format)
{
    Texture::Description desc {
        .type = Texture::Type::Cubemap,
        .arrayCount = 1u,
        .extent = Extent3D(extent, 1),
        .format = format,
        .filter = Texture::Filters::linear(),
        .wrapMode = ImageWrapModes::clampAllToEdge(),
        .mipmap = Texture::Mipmap::None,
        .multisampling = Texture::Multisampling::None
    };

    return createTexture(desc);
}

std::pair<Texture&, Registry::ReuseMode> Registry::createOrReuseTexture2D(const std::string& name, Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, ImageWrapModes wrapMode)
{
    if (m_previousRegistry) {
        for (std::unique_ptr<Texture>& oldTexture : m_previousRegistry->m_textures) {
            if (oldTexture && oldTexture->reusable({}) && oldTexture->name() == name) {

                // Verify that all parameters are the same (for reuse we obviouly need that it's the same between occations)
                ARKOSE_ASSERT(extent == oldTexture->extent());
                ARKOSE_ASSERT(format == oldTexture->format());
                ARKOSE_ASSERT(filters.min == oldTexture->minFilter());
                ARKOSE_ASSERT(filters.mag == oldTexture->magFilter());
                ARKOSE_ASSERT(mipmap == oldTexture->mipmap());
                ARKOSE_ASSERT(wrapMode.u == oldTexture->wrapMode().u);
                ARKOSE_ASSERT(wrapMode.v == oldTexture->wrapMode().v);
                ARKOSE_ASSERT(wrapMode.w == oldTexture->wrapMode().w);

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

Texture& Registry::createOrReuseTextureArray(const std::string& name, uint32_t itemCount, Extent2D extent, Texture::Format format, Texture::Filters filters, Texture::Mipmap mipmap, ImageWrapModes wrapMode)
{
    if (m_previousRegistry) {
        for (std::unique_ptr<Texture>& oldTexture : m_previousRegistry->m_textures) {
            if (oldTexture && oldTexture->reusable({}) && oldTexture->name() == name) {

                // Verify that all parameters are the same (for reuse we obviouly need that it's the same between occations)
                ARKOSE_ASSERT(itemCount == oldTexture->arrayCount());
                ARKOSE_ASSERT(extent == oldTexture->extent());
                ARKOSE_ASSERT(format == oldTexture->format());
                ARKOSE_ASSERT(filters.min == oldTexture->minFilter());
                ARKOSE_ASSERT(filters.mag == oldTexture->magFilter());
                ARKOSE_ASSERT(mipmap == oldTexture->mipmap());
                ARKOSE_ASSERT(wrapMode.u == oldTexture->wrapMode().u);
                ARKOSE_ASSERT(wrapMode.v == oldTexture->wrapMode().v);
                ARKOSE_ASSERT(wrapMode.w == oldTexture->wrapMode().w);

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
        ARKOSE_LOG(Warning, "Warning: creating buffer of size 0!");

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
    auto texture = Texture::createFromPixel(m_backend, pixelValue, srgb);
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

Texture& Registry::loadTextureArrayFromFileSequence(const std::string& imagePathPattern, bool srgb, bool generateMipmaps)
{
    SCOPED_PROFILE_ZONE();

    auto texture = Texture::createFromImagePathSequence(backend(), imagePathPattern, srgb, generateMipmaps, ImageWrapModes::clampAllToEdge());
    texture->setOwningRegistry({}, this);

    m_textures.push_back(std::move(texture));
    return *m_textures.back();
}

RenderState& Registry::createRenderState(const RenderStateBuilder& builder)
{
    return createRenderState(builder.renderTarget, builder.vertexLayouts, builder.shader,
                             builder.stateBindings(), builder.rasterState(), builder.depthState(), builder.stencilState());
}

RenderState& Registry::createRenderState(
    const RenderTarget& renderTarget, const std::vector<VertexLayout>& vertexLayouts,
    const Shader& shader, const StateBindings& stateBindings,
    const RasterState& rasterState, const DepthState& depthState, const StencilState& stencilState)
{
    auto renderState = backend().createRenderState(renderTarget, vertexLayouts, shader, stateBindings, rasterState, depthState, stencilState);
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

TopLevelAS& Registry::createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance> initialInstances)
{
    auto tlas = backend().createTopLevelAccelerationStructure(maxInstanceCount, initialInstances);
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

ComputeState& Registry::createComputeState(Shader const& shader, StateBindings const& stateBindings)
{
    auto computeState = backend().createComputeState(shader, stateBindings);
    computeState->setOwningRegistry({}, this);

    m_computeStates.push_back(std::move(computeState));
    return *m_computeStates.back();
}

UpscalingState& Registry::createUpscalingState(UpscalingTech tech, UpscalingQuality quality, Extent2D renderRes, Extent2D outputDisplayRes)
{
    auto upscalingState = backend().createUpscalingState(tech, quality, renderRes, outputDisplayRes);
    upscalingState->setOwningRegistry({}, this);

    m_upscalingStates.push_back(std::move(upscalingState));
    return *m_upscalingStates.back();
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

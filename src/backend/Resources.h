#pragma once

#include "rendering/Shader.h"
#include "rendering/scene/Transform.h"
#include "utility/Badge.h"
#include "utility/Extent.h"
#include "utility/util.h"
#include <cstdint>
#include <functional>
#include <mooslib/vector.h>
#include <optional>
#include <string>
#include <variant>
#include <vector>

class Backend;
class Registry;

struct Resource {
public:
    Resource();
    explicit Resource(Backend&);
    virtual ~Resource() = default;

    Resource(Resource&) = delete;
    Resource& operator=(Resource&) = delete;

    Resource(Resource&&) noexcept;
    Resource& operator=(Resource&&) noexcept;

protected:
    bool hasBackend() const { return m_backend != nullptr; }
    Backend& backend() { return *m_backend; }

private:
    Backend* m_backend { nullptr };
};

struct ClearColor {
    ClearColor(float rgb[3], float a = 1.0f)
        : r(pow(rgb[0], 2.2f))
        , g(pow(rgb[1], 2.2f))
        , b(pow(rgb[2], 2.2f))
        , a(a)
    {
    }
    ClearColor(float r, float g, float b, float a = 1.0f)
        : r(pow(r, 2.2f))
        , g(pow(g, 2.2f))
        , b(pow(b, 2.2f))
        , a(a)
    {
    }

    float r { 0.0f };
    float g { 0.0f };
    float b { 0.0f };
    float a { 0.0f };
};

struct Texture : public Resource {

    // (required for now, so we can create the mockup Texture objects)
    friend class VulkanBackend;

    enum class Format {
        Unknown,
        R32,
        R16F,
        RGBA8,
        sRGBA8,
        RGBA16F,
        RGBA32F,
        Depth32F,
    };

    enum class MinFilter {
        Linear,
        Nearest,
    };

    enum class MagFilter {
        Linear,
        Nearest,
    };

    enum class Mipmap {
        None,
        Nearest,
        Linear,
    };

    enum class Multisampling : uint32_t {
        None = 1,
        X2 = 2,
        X4 = 4,
        X8 = 8,
        X16 = 16,
        X32 = 32,
    };

    Texture() = default;
    Texture(Backend&, Extent2D, Format, MinFilter, MagFilter, Mipmap, Multisampling);

    bool hasFloatingPointDataFormat() const;

    virtual void setPixelData(vec4 pixel) = 0;
    virtual void setData(const void* data, size_t size) = 0;

    [[nodiscard]] const Extent2D& extent() const { return m_extent; }
    [[nodiscard]] Format format() const { return m_format; }
    [[nodiscard]] MinFilter minFilter() const { return m_minFilter; }
    [[nodiscard]] MagFilter magFilter() const { return m_magFilter; }

    [[nodiscard]] Mipmap mipmap() const { return m_mipmap; }
    [[nodiscard]] bool hasMipmaps() const;
    [[nodiscard]] uint32_t mipLevels() const;

    [[nodiscard]] bool isMultisampled() const;
    [[nodiscard]] Multisampling multisampling() const;

    [[nodiscard]] bool hasDepthFormat() const
    {
        return m_format == Format::Depth32F;
    }

    [[nodiscard]] bool hasSrgbFormat() const
    {
        return m_format == Format::sRGBA8;
    }

private:
    Extent2D m_extent;
    Format m_format;
    MinFilter m_minFilter;
    MagFilter m_magFilter;
    Mipmap m_mipmap;
    Multisampling m_multisampling;
};

enum class LoadOp {
    Clear,
    Load,
};

enum class StoreOp {
    Ignore,
    Store,
};

struct RenderTarget : public Resource {

    // (required for now, so we can create the mockup RenderTarget objects)
    friend class VulkanBackend;

    enum class AttachmentType : unsigned int {
        Color0 = 0,
        Color1 = 1,
        Color2 = 2,
        Color3 = 3,
        Depth = UINT_MAX
    };

    struct Attachment {
        AttachmentType type { AttachmentType::Color0 };
        Texture* texture { nullptr };
        LoadOp loadOp { LoadOp::Clear };
        StoreOp storeOp { StoreOp::Store };
    };

    RenderTarget() = default;
    RenderTarget(Backend&, std::vector<Attachment> attachments);

    [[nodiscard]] const Extent2D& extent() const;
    [[nodiscard]] size_t colorAttachmentCount() const;
    [[nodiscard]] size_t totalAttachmentCount() const;
    [[nodiscard]] bool hasDepthAttachment() const;

    [[nodiscard]] Texture* attachment(AttachmentType) const;

    [[nodiscard]] const std::vector<Attachment>& sortedAttachments() const;

    void forEachColorAttachment(std::function<void(const Attachment&)>) const;

private:
    std::vector<Attachment> m_attachments {};
};

struct Buffer : public Resource {

    enum class Usage {
        Vertex,
        Index,
        UniformBuffer,
        StorageBuffer,
    };

    enum class MemoryHint {
        TransferOptimal,
        GpuOptimal,
        GpuOnly,
        Readback,
    };

    Buffer() = default;
    Buffer(Backend&, size_t size, Usage usage, MemoryHint);

    size_t size() const { return m_size; }
    Usage usage() const { return m_usage; }
    MemoryHint memoryHint() const { return m_memoryHint; }

    virtual void updateData(const std::byte* data, size_t size) = 0;

    template<typename T>
    void updateData(const T* data, size_t size)
    {
        auto* byteData = reinterpret_cast<const std::byte*>(data);
        updateData(byteData, size);
    }

private:
    size_t m_size { 0 };
    Usage m_usage { Usage::Vertex };
    MemoryHint m_memoryHint { MemoryHint::GpuOptimal };
};

enum class VertexAttributeType {
    Float2,
    Float3,
    Float4
};

struct VertexAttribute {
    uint32_t location {};
    VertexAttributeType type {};
    size_t memoryOffset {};
};

struct VertexLayout {
    size_t vertexStride {};
    std::vector<VertexAttribute> attributes {};
};

enum class IndexType {
    UInt16,
    UInt32,
};

struct BlendState {
    bool enabled { false };
};

struct DepthState {
    bool writeDepth { true };
    bool testDepth { true };
};

enum class TriangleWindingOrder {
    Clockwise,
    CounterClockwise
};

enum class PolygonMode {
    Filled,
    Lines,
    Points
};

struct RasterState {
    bool backfaceCullingEnabled { true };
    TriangleWindingOrder frontFace { TriangleWindingOrder::CounterClockwise };
    PolygonMode polygonMode { PolygonMode::Filled };
};

struct Viewport {
    float x { 0.0f };
    float y { 0.0f };
    Extent2D extent;
};

enum class PipelineStage {
    Host,
    RayTracing,
    // TODO: Add more obviously
};

enum ShaderStage : uint8_t {
    ShaderStageVertex = 0x01,
    ShaderStageFragment = 0x02,
    ShaderStageCompute = 0x04,
    ShaderStageRTRayGen = 0x08,
    ShaderStageRTMiss = 0x10,
    ShaderStageRTClosestHit = 0x20,
    ShaderStageRTAnyHit = 0x40,
    ShaderStageRTIntersection = 0x80,
};

enum class ShaderBindingType {
    UniformBuffer,
    StorageBuffer,
    StorageImage,
    TextureSampler,
    TextureSamplerArray,
    StorageBufferArray,
    RTAccelerationStructure,
};

class TopLevelAS;

struct ShaderBinding {

    // Single uniform or storage buffer
    ShaderBinding(uint32_t index, ShaderStage, Buffer*);

    // Single sampled texture or storage image
    ShaderBinding(uint32_t index, ShaderStage, Texture*, ShaderBindingType);

    // Single top level acceleration structures
    ShaderBinding(uint32_t index, ShaderStage, TopLevelAS*);

    // Multiple sampled textures in an array of fixed size (count)
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<Texture*>&, uint32_t count);

    // Multiple storage buffers in a dynamic array
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<Buffer*>&);

    uint32_t bindingIndex;
    uint32_t count;

    ShaderStage shaderStage;

    ShaderBindingType type;
    TopLevelAS* tlas;
    std::vector<Buffer*> buffers;
    std::vector<Texture*> textures;
};

struct BindingSet : public Resource {
    BindingSet() = default;
    BindingSet(Backend&, std::vector<ShaderBinding>);

    const std::vector<ShaderBinding>& shaderBindings() const;

private:
    std::vector<ShaderBinding> m_shaderBindings {};
};

struct RenderState : public Resource {
public:
    RenderState() = default;
    RenderState(Backend& backend,
                const RenderTarget& renderTarget, VertexLayout vertexLayout,
                Shader shader, const std::vector<const BindingSet*>& shaderBindingSets,
                Viewport viewport, BlendState blendState, RasterState rasterState, DepthState depthState)
        : Resource(backend)
        , m_renderTarget(&renderTarget)
        , m_vertexLayout(vertexLayout)
        , m_shader(shader)
        , m_shaderBindingSets(shaderBindingSets)
        , m_viewport(viewport)
        , m_blendState(blendState)
        , m_rasterState(rasterState)
        , m_depthState(depthState)
    {
        ASSERT(shader.type() == ShaderType::Raster);
    }

    const RenderTarget& renderTarget() const { return *m_renderTarget; }
    const VertexLayout& vertexLayout() const { return m_vertexLayout; }

    const Shader& shader() const { return m_shader; }
    const std::vector<const BindingSet*>& bindingSets() const { return m_shaderBindingSets; }

    const Viewport& fixedViewport() const { return m_viewport; }
    const BlendState& blendState() const { return m_blendState; }
    const RasterState& rasterState() const { return m_rasterState; }
    const DepthState& depthState() const { return m_depthState; }

private:
    const RenderTarget* m_renderTarget;
    VertexLayout m_vertexLayout;

    Shader m_shader;
    std::vector<const BindingSet*> m_shaderBindingSets;

    Viewport m_viewport;
    BlendState m_blendState;
    RasterState m_rasterState;
    DepthState m_depthState;
};

class RenderStateBuilder {
public:
    RenderStateBuilder(const RenderTarget&, const Shader&, VertexLayout);

    const RenderTarget& renderTarget;
    const Shader& shader;
    VertexLayout vertexLayout;

    bool writeDepth { true };
    bool testDepth { true };
    PolygonMode polygonMode { PolygonMode::Filled };

    [[nodiscard]] Viewport viewport() const;
    [[nodiscard]] BlendState blendState() const;
    [[nodiscard]] RasterState rasterState() const;
    [[nodiscard]] DepthState depthState() const;

    RenderStateBuilder& addBindingSet(const BindingSet&);
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const;

private:
    std::optional<Viewport> m_viewport {};
    std::optional<BlendState> m_blendState {};
    std::optional<RasterState> m_rasterState {};
    std::vector<const BindingSet*> m_bindingSets {};
};

enum class RTVertexFormat {
    XYZ32F
};

struct RTTriangleGeometry {
    const Buffer& vertexBuffer;
    RTVertexFormat vertexFormat;
    size_t vertexStride;

    const Buffer& indexBuffer;
    IndexType indexType;

    mat4 transform;
};

struct RTAABBGeometry {
    const Buffer& aabbBuffer;
    size_t aabbStride;
};

class RTGeometry {
public:
    RTGeometry(RTTriangleGeometry);
    RTGeometry(RTAABBGeometry);

    bool hasTriangles() const;
    bool hasAABBs() const;

    const RTTriangleGeometry& triangles() const;
    const RTAABBGeometry& aabbs() const;

private:
    std::variant<RTTriangleGeometry, RTAABBGeometry> m_internal;
};

class BottomLevelAS : public Resource {
public:
    BottomLevelAS() = default;
    BottomLevelAS(Backend&, std::vector<RTGeometry>);

    [[nodiscard]] const std::vector<RTGeometry>& geometries() const;

private:
    std::vector<RTGeometry> m_geometries {};
};

struct RTGeometryInstance {
    const BottomLevelAS& blas;
    const Transform& transform;
    uint32_t shaderBindingTableOffset;
    uint32_t customInstanceId;
    uint8_t hitMask;
};

class TopLevelAS : public Resource {
public:
    TopLevelAS() = default;
    TopLevelAS(Backend&, std::vector<RTGeometryInstance>);

    [[nodiscard]] const std::vector<RTGeometryInstance>& instances() const;
    [[nodiscard]] uint32_t instanceCount() const;

private:
    std::vector<RTGeometryInstance> m_instances {};
};

class HitGroup {
public:
    explicit HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit = {}, std::optional<ShaderFile> intersection = {});

    const ShaderFile& closestHit() const { return m_closestHit; }

    bool hasAnyHitShader() const { return m_anyHit.has_value(); }
    const ShaderFile& anyHit() const { return m_anyHit.value(); }

    bool hasIntersectionShader() const { return m_intersection.has_value(); }
    const ShaderFile& intersection() const { return m_intersection.value(); }

private:
    ShaderFile m_closestHit;
    std::optional<ShaderFile> m_anyHit;
    std::optional<ShaderFile> m_intersection;
};

class ShaderBindingTable {
public:
    // See https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways for all info you might want about SBT stuff!
    // TODO: Add support for ShaderRecord instead of just shader file, so we can include parameters to the records.

    ShaderBindingTable() = default;
    ShaderBindingTable(ShaderFile rayGen, std::vector<HitGroup> hitGroups, std::vector<ShaderFile> missShaders);

    const ShaderFile& rayGen() const { return m_rayGen; }
    const std::vector<HitGroup>& hitGroups() const { return m_hitGroups; }
    const std::vector<ShaderFile>& missShaders() const { return m_missShaders; }

    std::vector<ShaderFile> allReferencedShaderFiles() const;

private:
    // TODO: In theory we can have more than one ray gen shader!
    ShaderFile m_rayGen;
    std::vector<HitGroup> m_hitGroups;
    std::vector<ShaderFile> m_missShaders;
};

class RayTracingState : public Resource {
public:
    RayTracingState() = default;
    RayTracingState(Backend&, ShaderBindingTable, std::vector<const BindingSet*>, uint32_t maxRecursionDepth);

    [[nodiscard]] uint32_t maxRecursionDepth() const;
    [[nodiscard]] const ShaderBindingTable& shaderBindingTable() const;
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const;

private:
    ShaderBindingTable m_shaderBindingTable;
    std::vector<const BindingSet*> m_bindingSets;
    uint32_t m_maxRecursionDepth;
};

class ComputeState : public Resource {
public:
    ComputeState() = default;
    ComputeState(Backend&, Shader, std::vector<const BindingSet*>);

    const Shader& shader() const { return m_shader; }
    [[nodiscard]] const std::vector<const BindingSet*>& bindingSets() const { return m_bindingSets; }

private:
    Shader m_shader;
    std::vector<const BindingSet*> m_bindingSets;
};

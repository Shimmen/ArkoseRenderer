#pragma once

#if JPH_DEBUG_RENDERER

#include <ark/color.h>
#include <ark/vector.h>
#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

class JoltVisualiser final : public JPH::DebugRenderer {

public:

    JoltVisualiser();
    virtual ~JoltVisualiser();

    // JPH::DebugRenderer interface
    virtual void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override;
    virtual void DrawTriangle(JPH::Vec3Arg V1, JPH::Vec3Arg V2, JPH::Vec3Arg V3, JPH::ColorArg color, ECastShadow) override;
    virtual JPH::DebugRenderer::Batch CreateTriangleBatch(const JPH::DebugRenderer::Triangle* triangles, int triangleCount) override;
    virtual JPH::DebugRenderer::Batch CreateTriangleBatch(const JPH::DebugRenderer::Vertex* vertices, int vertexCount, const uint32_t* indices, int indexCount) override;
    virtual void DrawGeometry(JPH::Mat44Arg modelMatrix, const JPH::AABox& worldSpaceBounds, float lodScale2, JPH::ColorArg, const GeometryRef&, ECullMode, ECastShadow, EDrawMode) override;
    virtual void DrawText3D(JPH::Vec3Arg position, const std::string_view& string, JPH::ColorArg color, float height) override;

private:

    Color joltColorToArkColor(JPH::ColorArg) const;

    // Implementation specific batch object
    class ArkoseBatch : public JPH::RefTargetVirtual {
    public:

        ArkoseBatch(uint32_t id)
            : m_id(id)
        {
        }

        virtual void AddRef() override { ++m_refCount; }
        virtual void Release() override
        {
            if (--m_refCount == 0)
                delete this;
        }

        std::atomic<uint32_t> m_refCount { 0 };
        uint32_t m_id {};
    };

    uint32_t m_nextBatchId { 0 };

    // TODO: pre-allocated vertex and index buffers

};

#endif // JPH_DEBUG_RENDERER

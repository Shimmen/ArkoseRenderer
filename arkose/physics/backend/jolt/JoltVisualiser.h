#pragma once

#include "physics/backend/base/PhysicsVisualiser.h"

#if JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

class JoltVisualiser final : public JPH::DebugRenderer, PhysicsVisualiser {

public:

    JoltVisualiser();
    virtual ~JoltVisualiser();

    // PhysicsVisualiser interface

    virtual void drawStuff(/* ... */) override;

    // JPH::DebugRenderer interface

    virtual void DrawLine(const JPH::Float3& from, const JPH::Float3& to, JPH::ColorArg color) override;
    virtual void DrawTriangle(JPH::Vec3Arg V1, JPH::Vec3Arg V2, JPH::Vec3Arg V3, JPH::ColorArg color) override;
    virtual JPH::DebugRenderer::Batch CreateTriangleBatch(const JPH::DebugRenderer::Triangle* triangles, int triangleCount) override;
    virtual JPH::DebugRenderer::Batch CreateTriangleBatch(const JPH::DebugRenderer::Vertex* vertices, int vertexCount, const uint32_t* indices, int indexCount) override;
    virtual void DrawGeometry(JPH::Mat44Arg modelMatrix, const JPH::AABox& worldSpaceBounds, float lodScale2, JPH::ColorArg, const GeometryRef&, ECullMode, ECastShadow, EDrawMode) override;
    virtual void DrawText3D(JPH::Vec3Arg position, const std::string_view& string, JPH::ColorArg color, float height) override;

private:

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
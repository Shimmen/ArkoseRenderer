#pragma once

class Icon;

#include <ark/color.h>
#include <ark/handle.h>
#include "rendering/RenderPipelineNode.h"
#include "rendering/ResourceList.h"
#include "rendering/debug/DebugDrawer.h"

ARK_DEFINE_HANDLE_TYPE(DebugTextureBindingSetHandle);

class DebugDrawNode final : public RenderPipelineNode, public IDebugDrawer {
public:
    DebugDrawNode();
    virtual ~DebugDrawNode();

    std::string name() const override { return "Debug drawing"; }
    void drawGui() override;

    ExecuteCallback construct(GpuScene&, Registry&) override;

    // IDebugDrawer implementation
    virtual void drawLine(vec3 p0, vec3 p1, Color color) override;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, Color color) override;
    virtual void drawSphere(vec3 center, float radius, Color color) override;
    virtual void drawIcon(IconBillboard const&, Color tint) override;
    virtual void drawSkeleton(Skeleton const&, mat4 rootTransform, Color color) override;

private:
    Backend* m_backend { nullptr };

    struct DebugDrawVertex {
        DebugDrawVertex(vec3 inPosition, vec3 inColor)
            : position(inPosition)
            , color(inColor)
        {
        }

        vec3 position;
        vec3 color;
    };

    static constexpr size_t MaxNumLineSegments = 4096;
    static constexpr size_t LineVertexBufferSize = MaxNumLineSegments * 2 * sizeof(DebugDrawVertex);
    std::vector<DebugDrawVertex> m_lineVertices {};
    Buffer* m_lineVertexBuffer { nullptr };

    struct DebugDrawTexturedVertex {
        DebugDrawTexturedVertex(vec3 inPosition, vec3 inColor, vec2 inTexCoord)
            : position(inPosition)
            , color(inColor)
            , texCoord(inTexCoord)
        {
        }

        vec3 position;
        vec3 color;
        vec2 texCoord;
    };

    static constexpr size_t MaxNumTriangles = 40 * 1024;
    static constexpr size_t TriangleVertexBufferSize = MaxNumTriangles * 3 * sizeof(DebugDrawTexturedVertex);
    std::vector<DebugDrawTexturedVertex> m_triangleVertices {};
    Buffer* m_triangleVertexBuffer { nullptr };

    struct DebugDrawMesh {
        u32 numVertices { 0 };
        u32 firstVertex { 0 };
        DebugTextureBindingSetHandle textureBindingSetHandle {};
    };

    std::vector<DebugDrawMesh> m_debugDrawMeshes {};

    ResourceList<std::unique_ptr<BindingSet>, DebugTextureBindingSetHandle> m_debugDrawTextures { "DebugTextures", 256 };
    DebugTextureBindingSetHandle m_whiteDebugDrawTexture {};

    DebugTextureBindingSetHandle createIconTextureBindingSet(Icon const*);
    DebugTextureBindingSetHandle createDebugTextureBindingSet(Texture const*);
};

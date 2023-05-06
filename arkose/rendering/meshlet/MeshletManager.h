#pragma once

#include "asset/StaticMeshAsset.h"
#include "core/NonCopyable.h"
#include "core/Types.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/IndexType.h"
#include "rendering/StaticMesh.h"
#include "scene/Vertex.h"
#include <memory>
#include <unordered_set>

class Backend;
class CommandList;
class StaticMesh;
class UploadBuffer;
struct StaticMeshSegment;

// Shader headers
#include "shaders/shared/SceneData.h"

class MeshletManager {
public:
    explicit MeshletManager(Backend&);
    ~MeshletManager();

    NON_COPYABLE(MeshletManager);

    void allocateMeshlets(StaticMesh&);
    void freeMeshlets(StaticMesh&);

    void processMeshStreaming(CommandList& cmdList, std::unordered_set<StaticMeshHandle>& updatedMeshes);

    std::vector<ShaderMeshlet> const& meshlets() const { return m_meshlets; }
    Buffer const& meshletBuffer() const { return *m_meshletBuffer; }

    Buffer const& meshletPositionDataVertexBuffer() const { return *m_positionDataVertexBuffer; }
    Buffer const& meshletNonPositionDataVertexBuffer() const { return *m_nonPositionDataVertexBuffer; }

    Buffer const& meshletIndexBuffer() const { return *m_indexBuffer; }
    IndexType meshletIndexType() const { return IndexType::UInt32; }
    u32 meshletIndexCount() const { return m_nextIndexIdx; } // TODO: Assuming no freed meshes..

    // Max that can be loaded in the GPU at any time
    // TODO: Optimize these sizes!
    static constexpr size_t MaxLoadedVertices = 5'000'000;
    static constexpr size_t MaxLoadedTriangles = 10'000'000;
    static constexpr size_t MaxLoadedIndices = 3 * MaxLoadedTriangles;
    static constexpr size_t MaxLoadedMeshlets = MaxLoadedTriangles / 124;

    static constexpr size_t UploadBufferSize = 10 * 1024 * 1024;

private:
    const VertexLayout m_positionVertexLayout { VertexComponent::Position3F };
    const VertexLayout m_nonPositionVertexLayout { VertexComponent::TexCoord2F,
                                                   VertexComponent::Normal3F,
                                                   VertexComponent::Tangent4F };

    std::unique_ptr<Buffer> m_positionDataVertexBuffer { nullptr };
    std::unique_ptr<Buffer> m_nonPositionDataVertexBuffer { nullptr };
    std::unique_ptr<Buffer> m_indexBuffer { nullptr };

    std::vector<ShaderMeshlet> m_meshlets {};
    std::unique_ptr<Buffer> m_meshletBuffer { nullptr };

    u32 m_nextVertexIdx { 0 };
    u32 m_nextIndexIdx { 0 };
    u32 m_nextMeshletIdx { 0 };

    std::vector<StaticMeshSegment*> m_segmentsAwaitingUpload {};
    std::unique_ptr<UploadBuffer> m_uploadBuffer {};
};

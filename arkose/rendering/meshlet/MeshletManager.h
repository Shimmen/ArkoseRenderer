#pragma once

#include <ark/copying.h>
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

    ARK_NON_COPYABLE(MeshletManager);

    void allocateMeshlets(StaticMesh&);
    void freeMeshlets(StaticMesh&);

    void processMeshStreaming(CommandList& cmdList, std::unordered_set<StaticMeshHandle>& updatedMeshes);

    std::vector<ShaderMeshlet> const& meshlets() const { return m_meshlets; }
    Buffer const& meshletBuffer() const { return *m_meshletBuffer; }

    Buffer const& meshletVertexIndirectionBuffer() const { return *m_vertexIndirectionBuffer; }

    Buffer const& meshletIndexBuffer() const { return *m_indexBuffer; }
    IndexType meshletIndexType() const { return IndexType::UInt32; }
    u32 meshletIndexCount() const { return m_nextIndexIdx; } // TODO: Assuming no freed meshes..

    static constexpr size_t UploadBufferSize = 4 * 1024 * 1024;

private:
    std::unique_ptr<Buffer> m_vertexIndirectionBuffer { nullptr };
    std::unique_ptr<Buffer> m_indexBuffer { nullptr };

    std::vector<ShaderMeshlet> m_meshlets {};
    std::unique_ptr<Buffer> m_meshletBuffer { nullptr };

    u32 m_nextVertexIndirectionIdx { 0 };
    u32 m_nextIndexIdx { 0 };
    u32 m_nextMeshletIdx { 0 };

    std::vector<StaticMeshSegment*> m_segmentsAwaitingUpload {};
    std::unique_ptr<UploadBuffer> m_uploadBuffer {};
};

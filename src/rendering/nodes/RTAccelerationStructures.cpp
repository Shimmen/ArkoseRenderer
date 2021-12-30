#include "RTAccelerationStructures.h"

#include "RTData.h"

RTAccelerationStructures::RTAccelerationStructures(Scene& scene)
    : RenderGraphNode(RTAccelerationStructures::name())
    , m_scene(scene)
{
}

std::string RTAccelerationStructures::name()
{
    return "rt-acceleration-structures";
}

void RTAccelerationStructures::constructNode(Registry& nodeReg)
{
    m_mainInstances.clear();

    uint32_t nextTriangleInstanceId = 0;

    m_scene.forEachModel([&](size_t, Model& model) {
        model.forEachMesh([&](Mesh& mesh) {
            RTGeometry geometry = createGeometryForTriangleMesh(mesh, nodeReg);
            uint8_t hitMask = HitMask::TriangleMeshWithoutProxy;
            RTGeometryInstance instance = createGeometryInstance(geometry, model.transform(), nextTriangleInstanceId++, hitMask, HitGroupIndex::Triangle, nodeReg);
            m_mainInstances.push_back(instance);
        });
    });
}

RenderGraphNode::ExecuteCallback RTAccelerationStructures::constructFrame(Registry& reg) const
{
    TopLevelAS& main = reg.createTopLevelAccelerationStructure(m_mainInstances);
    reg.publish("rtAccStructureNodeScene", main);

    return [&](const AppState& appState, CommandList& cmdList) {
        cmdList.rebuildTopLevelAcceratationStructure(main);
    };
}

RTGeometry RTAccelerationStructures::createGeometryForTriangleMesh(Mesh& mesh, Registry& reg) const
{
    RTTriangleGeometry geometry { .vertexBuffer = reg.createBuffer(mesh.vertexData({ VertexComponent::Position3F }), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal),
                                  .vertexCount = (uint32_t)mesh.vertexCountForLayout({ VertexComponent::Position3F }),
                                  .vertexOffset = 0,
                                  .vertexStride = sizeof(vec3),
                                  .vertexFormat = RTVertexFormat::XYZ32F,
                                  .indexBuffer = reg.createBuffer(mesh.indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal),
                                  .indexCount = (uint32_t)mesh.indexCount(),
                                  .indexOffset = 0,
                                  .indexType = mesh.indexType(),
                                  .transform = mesh.transform().localMatrix() };
    return geometry;
}

RTGeometryInstance RTAccelerationStructures::createGeometryInstance(const RTGeometry& geometry, const Transform& transform, uint32_t customId, uint8_t hitMask, uint32_t sbtOffset, Registry& reg) const
{
    // TODO: Later we probably want to keep all meshes of a model in a single BLAS, but that requires some fancy SBT stuff which I don't wanna mess with now.
    BottomLevelAS& blas = reg.createBottomLevelAccelerationStructure({ geometry });
    RTGeometryInstance instance = { .blas = blas,
                                    .transform = transform,
                                    .shaderBindingTableOffset = sbtOffset,
                                    .customInstanceId = customId,
                                    .hitMask = hitMask };
    return instance;
}

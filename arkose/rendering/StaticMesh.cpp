#include "StaticMesh.h"

#include "asset/StaticMeshAsset.h"
#include "rendering/backend/base/AccelerationStructure.h"
#include "rendering/GpuScene.h"

StaticMesh::StaticMesh(StaticMeshAsset* asset)
    : m_asset(asset)
{
}

size_t StaticMeshSegment::vertexCount() const
{
    size_t count = positions.size();
    
    ARKOSE_ASSERT(normals.size() == count);
    //ARKOSE_ASSERT(texcoord0s.size() == count);
    //ARKOSE_ASSERT(tangents.size() == count); // TODO: Ensure we always have tangents!

    return count;
}

std::vector<uint8_t> StaticMeshSegment::assembleVertexData(const VertexLayout& layout) const
{
    SCOPED_PROFILE_ZONE();

    size_t packedVertexSize = layout.packedVertexSize();
    size_t bufferSize = vertexCount() * packedVertexSize;

    std::vector<uint8_t> dataVector {};
    dataVector.resize(bufferSize);
    uint8_t* data = dataVector.data();

    // FIXME: This only really works for float components. Later we need a way of doing this for other types as well,
    //  but right now we only have floating point components anyway.
    constexpr std::array<float, 4> floatOnes { 1, 1, 1, 1 };

    size_t offsetInFirstVertex = 0u;

    auto copyComponentData = [&](const uint8_t* input, size_t inputCount, VertexComponent component) {
        size_t componentSize = vertexComponentSize(component);
        for (size_t vertexIdx = 0, count = vertexCount(); vertexIdx < count; ++vertexIdx) {
            uint8_t* destination = data + offsetInFirstVertex + vertexIdx * packedVertexSize;
            const uint8_t* source = (vertexIdx < inputCount)
                ? &input[vertexIdx * componentSize]
                : (uint8_t*)floatOnes.data();
            std::memcpy(destination, source, componentSize);
        }
        return componentSize;
    };

    for (auto& component : layout.components()) {
        switch (component) {
        case VertexComponent::Position3F: {
            auto* inputData = (const uint8_t*)value_ptr(*positions.data());
            offsetInFirstVertex += copyComponentData(inputData, positions.size(), component);
        } break;
        case VertexComponent::Normal3F: {
            auto* inputData = (const uint8_t*)value_ptr(*normals.data());
            offsetInFirstVertex += copyComponentData(inputData, normals.size(), component);
        } break;
        case VertexComponent::TexCoord2F: {
            auto* inputData = (const uint8_t*)value_ptr(*texcoord0s.data());
            offsetInFirstVertex += copyComponentData(inputData, texcoord0s.size(), component);
        } break;
        case VertexComponent::Tangent4F: {
            auto* inputData = (const uint8_t*)value_ptr(*tangents.data());
            offsetInFirstVertex += copyComponentData(inputData, tangents.size(), component);
        } break;
        }
    }

    return dataVector;
}

void StaticMeshSegment::ensureDrawCallIsAvailable(const VertexLayout& layout, GpuScene& scene) const
{
    SCOPED_PROFILE_ZONE();
    // Will create the relevant buffers & set their data (if it doesn't already exist)
    drawCallDescription(layout, scene);
}

const DrawCallDescription& StaticMeshSegment::drawCallDescription(const VertexLayout& layout, GpuScene& scene) const
{
    SCOPED_PROFILE_ZONE();

    auto entry = m_drawCallDescriptions.find(layout);
    if (entry != m_drawCallDescriptions.end())
        return entry->second;

    // This specific vertex layout has not yet been fitted to the vertex buffer but there are at least one other layout setup.
    // All subsequent layouts should replicate the offsets etc. since it means we can reuse index data & also can expect that
    // vertex layouts line up w.r.t. the DrawCallDescription. This is good if you e.g. cull, then z-prepass with position-only,
    // and then draw objects normally with a full layout. If they line up we can use the indirect culling draw commands for both!

    std::optional<DrawCallDescription> previousToAlignWith {};
    if (m_drawCallDescriptions.size() > 0) {
        previousToAlignWith = m_drawCallDescriptions.begin()->second;
    }

    DrawCallDescription drawCallDescription = scene.fitVertexAndIndexDataForMesh({}, *this, layout, previousToAlignWith);

    m_drawCallDescriptions[layout] = drawCallDescription;
    return m_drawCallDescriptions[layout];
}

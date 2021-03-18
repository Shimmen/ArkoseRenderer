#include "Mesh.h"

#include "rendering/Registry.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <array>

Material& Mesh::material()
{
    if (!m_material)
        m_material = createMaterial();
    return *m_material;
}

std::vector<uint8_t> Mesh::vertexData(const VertexLayout& layout) const
{
    SCOPED_PROFILE_ZONE()

    size_t vertexCount = vertexCountForLayout(layout);

    size_t packedVertexSize = layout.packedVertexSize();
    size_t bufferSize = vertexCount * packedVertexSize;

    std::vector<uint8_t> dataVector {};
    dataVector.resize(bufferSize);
    uint8_t* data = dataVector.data();

    // FIXME: This only really works for float components. Later we need a way of doing this for other types as well,
    //  but right now we only have floating point components anyway.
    constexpr std::array<float, 4> floatOnes { 1, 1, 1, 1 };

    size_t offsetInFirstVertex = 0u;

    auto copyComponentData = [&](const uint8_t* input, size_t inputCount, VertexComponent component) {
        size_t componentSize = vertexComponentSize(component);
        for (size_t vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {
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
            auto& inputVector = positionData();
            auto* inputData = (const uint8_t*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        case VertexComponent::Normal3F: {
            auto& inputVector = normalData();
            auto* inputData = (const uint8_t*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        case VertexComponent::TexCoord2F: {
            auto& inputVector = texcoordData();
            auto* inputData = (const uint8_t*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        case VertexComponent::Tangent4F: {
            auto& inputVector = tangentData();
            auto* inputData = (const uint8_t*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        }
    }

    return dataVector;
}

size_t Mesh::vertexCountForLayout(const VertexLayout& layout) const
{
    size_t vertexCount = 0u;
    for (auto& component : layout.components()) {
        switch (component) {
        case VertexComponent::Position3F:
            vertexCount = std::max(vertexCount, positionData().size());
            break;
        case VertexComponent::Normal3F:
            vertexCount = std::max(vertexCount, normalData().size());
            break;
        case VertexComponent::TexCoord2F:
            vertexCount = std::max(vertexCount, texcoordData().size());
            break;
        case VertexComponent::Tangent4F:
            vertexCount = std::max(vertexCount, tangentData().size());
            break;
        }
    }

    return vertexCount;
}

void Mesh::ensureDrawCallIsReady(const VertexLayout& layout, Scene& scene)
{
    SCOPED_PROFILE_ZONE();
    // Will create the relevant buffers & set their data (if it doesn't already exist)
    drawCallDescription(layout, scene);
}

const DrawCallDescription& Mesh::drawCallDescription(const VertexLayout& layout, Scene& scene)
{
    SCOPED_PROFILE_ZONE()

    auto entry = m_drawCallDescriptions.find(layout);
    if (entry != m_drawCallDescriptions.end())
        return entry->second;

    DrawCallDescription drawCallDescription = scene.fitVertexAndIndexDataForMesh({}, *this, layout);

    m_drawCallDescriptions[layout] = drawCallDescription;
    return drawCallDescription;
}

#include "Mesh.h"

#include "rendering/Registry.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"
#include "utility/Logging.h"
#include <array>

Material& Mesh::material()
{
    if (!m_material)
        m_material = createMaterial();
    return *m_material;
}

void Mesh::ensureVertexBuffer(const SemanticVertexLayout& layout)
{
    // NOTE: Will create & cache the buffer (if it doesn't already exist)
    vertexBuffer(layout);
}

const Buffer& Mesh::vertexBuffer(const SemanticVertexLayout& layout)
{
    auto entry = m_vertexBuffers.find(layout);
    if (entry != m_vertexBuffers.end())
        return *entry->second;

    if (!model())
        LogErrorAndExit("Mesh: can't request vertex buffer for mesh that is not part of a model, exiting\n");
    if (!model()->scene())
        LogErrorAndExit("Mesh: can't request vertex buffer for mesh that is not part of a scene, exiting\n");

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

    size_t packedVertexSize = layout.packedVertexSize();
    size_t bufferSize = vertexCount * packedVertexSize;

    auto* data = (moos::u8*)malloc(bufferSize);
    MOOSLIB_ASSERT(data);

    // FIXME: This only really works for float components. Later we need a way of doing this for other types as well,
    //  but right now we only have floating point components anyway.
    constexpr std::array<float, 4> floatZeros {};

    size_t offsetInFirstVertex = 0u;

    auto copyComponentData = [&](const moos::u8* input, size_t inputCount, VertexComponent component) {
        size_t componentSize = vertexComponentSize(component);
        for (size_t vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {
            moos::u8* destination = data + offsetInFirstVertex + vertexIdx * packedVertexSize;
            const moos::u8* source = (vertexIdx < inputCount)
                ? &input[vertexIdx * componentSize]
                : (moos::u8*)floatZeros.data();
            std::memcpy(destination, source, componentSize);
        }
        return componentSize;
    };

    for (auto& component : layout.components()) {
        switch (component) {
        case VertexComponent::Position3F: {
            auto& inputVector = positionData();
            auto* inputData = (const moos::u8*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        case VertexComponent::Normal3F: {
            auto& inputVector = normalData();
            auto* inputData = (const moos::u8*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        case VertexComponent::TexCoord2F: {
            auto& inputVector = texcoordData();
            auto* inputData = (const moos::u8*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        case VertexComponent::Tangent4F: {
            auto& inputVector = tangentData();
            auto* inputData = (const moos::u8*)value_ptr(*inputVector.data());
            offsetInFirstVertex += copyComponentData(inputData, inputVector.size(), component);
        } break;
        }
    }

    Registry& sceneRegistry = model()->scene()->registry();
    Buffer& vertexBuffer = sceneRegistry.createBuffer((std::byte*)data, bufferSize, Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOptimal);

    m_vertexBuffers[layout] = &vertexBuffer;
    return vertexBuffer;
}

void Mesh::ensureIndexBuffer()
{
    // NOTE: Will create & cache the buffer (if it doesn't already exist)
    indexBuffer();
}

const Buffer& Mesh::indexBuffer()
{
    if (m_indexBuffer != nullptr)
        return *m_indexBuffer;

    if (!model())
        LogErrorAndExit("Mesh: can't request index buffer for mesh that is not part of a model, exiting\n");
    if (!model()->scene())
        LogErrorAndExit("Mesh: can't request index buffer for mesh/model that is not part of a scene, exiting\n");

    Registry& sceneRegistry = model()->scene()->registry();
    m_indexBuffer = &sceneRegistry.createBuffer(indexData(), Buffer::Usage::Index, Buffer::MemoryHint::GpuOptimal);
    return *m_indexBuffer;
}

VertexLayout Mesh::canonoicalVertexLayout()
{
    return VertexLayout {
        sizeof(CanonoicalVertex),
        { { 0, VertexAttributeType::Float3, offsetof(CanonoicalVertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(CanonoicalVertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(CanonoicalVertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(CanonoicalVertex, tangent) } }
    };
}

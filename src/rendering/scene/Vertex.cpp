#include "Vertex.h"

SemanticVertexLayout::SemanticVertexLayout(std::initializer_list<VertexComponent> components)
    : m_components(components)
{
}

bool SemanticVertexLayout::operator==(const SemanticVertexLayout& other) const
{
    if (componentCount() != other.componentCount())
        return false;

    for (size_t i = 0; i < componentCount(); ++i) {
        if (m_components[i] != other.m_components[i])
            return false;
    }

    return true;
}

size_t SemanticVertexLayout::packedVertexSize() const
{
    size_t size = 0u;
    for (const auto& component : m_components)
        size += vertexComponentSize(component);
    return size;
}

#include "Vertex.h"

VertexLayout::VertexLayout(std::initializer_list<VertexComponent> components)
    : m_components(components)
{
}

bool VertexLayout::operator==(const VertexLayout& other) const
{
    if (componentCount() != other.componentCount())
        return false;

    for (size_t i = 0; i < componentCount(); ++i) {
        if (m_components[i] != other.m_components[i])
            return false;
    }

    return true;
}

VertexLayout VertexLayout::replaceAllWithPaddingBut(VertexComponent savedComponent) const
{
    VertexLayout paddedLayout = *this;

    for (VertexComponent& component : paddedLayout.m_components) { 
        if (component != savedComponent) {
            switch (vertexComponentSize(component)) {
            case 8:
                component = VertexComponent::Padding2F;
                break;
            case 12:
                component = VertexComponent::Padding3F;
                break;
            case 16:
                component = VertexComponent::Padding4F;
                break;
            default:
                NOT_YET_IMPLEMENTED();
            }
        }
    }

    return paddedLayout;
}

size_t VertexLayout::packedVertexSize() const
{
    size_t size = 0u;
    for (const auto& component : m_components)
        size += vertexComponentSize(component);
    return size;
}

std::string VertexLayout::toString(bool includeTypeName) const
{
    std::string result {};

    if (includeTypeName) {
        result += "VertexLayout { ";
    }

    for (size_t i = 0, count = components().size(); i < count; ++i) {
        result += vertexComponentToString(components()[i]);
        if (i < count - 1) {
            result += " ";
        }
    }

    if (includeTypeName) {
        result += "}";
    }

    return result;
}

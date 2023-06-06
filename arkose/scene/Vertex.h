#pragma once

#include "core/Assert.h"
#include "core/Types.h"
#include <string>
#include <vector>

enum class VertexComponent : int {
    Position2F,
    Position3F,
    Normal3F,
    TexCoord2F,
    Tangent4F,
    Color3F,
    JointIdx4U32,
    JointWeight4F,
};

static constexpr size_t vertexComponentSize(VertexComponent component)
{
    switch (component) {
    case VertexComponent::Position3F:
    case VertexComponent::Normal3F:
    case VertexComponent::Color3F:
        return 3 * sizeof(float);
    case VertexComponent::Position2F:
    case VertexComponent::TexCoord2F:
        return 2 * sizeof(float);
    case VertexComponent::Tangent4F:
    case VertexComponent::JointWeight4F:
        return 4 * sizeof(float);
    case VertexComponent::JointIdx4U32:
        return 4 * sizeof(u32);
    default:
        ASSERT_NOT_REACHED();
    }
}

static constexpr const char* vertexComponentToString(VertexComponent component)
{
    switch (component) {
    case VertexComponent::Position3F:
        return "Position3F";
    case VertexComponent::Normal3F:
        return "Normal3F";
    case VertexComponent::Position2F:
        return "Position2F";
    case VertexComponent::TexCoord2F:
        return "TexCoord2F";
    case VertexComponent::Tangent4F:
        return "Tangent4F";
    case VertexComponent::Color3F:
        return "Color3F";
    case VertexComponent::JointIdx4U32:
        return "JointIdx4U32";
    case VertexComponent::JointWeight4F:
        return "JointWeight4F";
    default:
        ASSERT_NOT_REACHED();
    }
}

class VertexLayout {
public:
    VertexLayout(std::initializer_list<VertexComponent>);

    bool operator==(const VertexLayout&) const;

    size_t componentCount() const { return m_components.size(); }
    const std::vector<VertexComponent>& components() const { return m_components; }

    size_t packedVertexSize() const;

    std::string toString(bool includeTypeName = true) const;

private:
    std::vector<VertexComponent> m_components;
};

namespace std {

template<>
struct hash<VertexComponent> {
    std::size_t operator()(const VertexComponent& comp) const
    {
        auto val = static_cast<std::underlying_type<VertexComponent>::type>(comp);
        return std::hash<std::underlying_type<VertexComponent>::type>()(val);
    }
};

template<>
struct hash<VertexLayout> {
    std::size_t operator()(const VertexLayout& layout) const
    {
        // I cannot manage to wrestle with std::hash for a vector of enum class, so I will use Boost's hash_combine
        // (http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0814r0.pdf) algorithm to evaluate the hash for
        // the whole range of components.
        size_t seed = 0u;
        for (const VertexComponent& component : layout.components())
            seed ^= std::hash<VertexComponent>()(component) + 0x9e3779b9 + (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

}

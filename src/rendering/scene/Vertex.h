#pragma once

#include "utility/util.h"
#include <vector>

enum class VertexComponent : int {
    Position3F,
    Normal3F,
    TexCoord2F,
    Tangent4F,
};

static constexpr size_t vertexComponentSize(VertexComponent component)
{
    switch (component) {
    case VertexComponent::Position3F:
    case VertexComponent::Normal3F:
        return 3 * sizeof(float);
    case VertexComponent::TexCoord2F:
        return 2 * sizeof(float);
    case VertexComponent::Tangent4F:
        return 4 * sizeof(float);
    }

    ASSERT_NOT_REACHED();
    return 0;
}

class SemanticVertexLayout {
public:
    SemanticVertexLayout(std::initializer_list<VertexComponent>);

    bool operator==(const SemanticVertexLayout&) const;

    size_t componentCount() const { return m_components.size(); }
    const std::vector<VertexComponent>& components() const { return m_components; }

    size_t packedVertexSize() const;

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
struct hash<SemanticVertexLayout> {
    std::size_t operator()(const SemanticVertexLayout& layout) const
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

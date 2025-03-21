#pragma once

#include "core/Assert.h"
#include "core/Types.h"
#include "utility/Hash.h"
#include <ark/vector.h>
#include <cstdint>

struct Extent2D {
    constexpr Extent2D()
        : Extent2D(0, 0)
    {
    }
    constexpr Extent2D(uint32_t width, uint32_t height)
        : m_width(width)
        , m_height(height)
    {
    }
    constexpr Extent2D(int width, int height)
        : m_width(width)
        , m_height(height)
    {
        ARKOSE_ASSERT(width >= 0);
        ARKOSE_ASSERT(height >= 0);
    }

    [[nodiscard]] constexpr uint32_t width() const { return m_width; }
    [[nodiscard]] constexpr uint32_t height() const { return m_height; }

    [[nodiscard]] bool hasZeroArea() const { return m_width == 0 || m_height == 0; }

    float aspectRatio() const { return static_cast<float>(m_width) / static_cast<float>(m_height); }

    bool operator!=(const Extent2D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent2D& other) const
    {
        return m_width == other.m_width && m_height == other.m_height;
    }

    bool operator<(Extent2D const& other) const
    {
        return m_width < other.m_width && m_height < other.m_height;
    }

    Extent2D operator/(int factor) const
    {
        return { m_width / factor, m_height / factor };
    }

    Extent2D& operator/=(int factor)
    {
        m_width /= factor;
        m_height /= factor;
        return *this;
    }

    Extent2D shrinkOnAllSidesBy(int x) const
    {
        return { m_width - (2 * x), m_height - (2 * x) };
    }

    ark::vec2 inverse() const { return ark::vec2(1.0f / m_width, 1.0f / m_height); }

    ark::uvec2 asUIntVector() const { return ark::uvec2(m_width, m_height); }
    ark::ivec2 asIntVector() const { return ark::ivec2(m_width, m_height); }
    ark::vec2 asFloatVector() const { return ark::vec2(static_cast<float>(m_width), static_cast<float>(m_height)); }

    template<class Archive>
    void serialize(Archive&);

private:
    uint32_t m_width {};
    uint32_t m_height {};
};

struct Extent3D {
    constexpr Extent3D(uint32_t val = 0)
        : Extent3D(val, val, val)
    {
    }
    constexpr Extent3D(uint32_t width, uint32_t height, uint32_t depth)
        : m_width(width)
        , m_height(height)
        , m_depth(depth)
    {
    }
    constexpr Extent3D(const Extent2D& extent2d, uint32_t depth = 1)
        : Extent3D(extent2d.width(), extent2d.height(), depth)
    {
    }

    [[nodiscard]] constexpr uint32_t width() const { return m_width; }
    [[nodiscard]] constexpr uint32_t height() const { return m_height; }
    [[nodiscard]] constexpr uint32_t depth() const { return m_depth; }

    [[nodiscard]] bool hasZeroArea() const { return m_width == 0 || m_height == 0 || m_depth == 0; }

    bool operator!=(const Extent3D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent3D& other) const
    {
        return m_width == other.m_width && m_height == other.m_height && m_depth == other.m_depth;
    }

    Extent2D asExtent2D() const { return Extent2D(m_width, m_height); }

    ark::uvec3 asUIntVector() const { return ark::uvec3(m_width, m_height, m_depth); }
    ark::ivec3 asIntVector() const { return ark::ivec3(m_width, m_height, m_depth); }
    ark::vec3 asFloatVector() const {return ark::vec3(static_cast<float>(m_width), static_cast<float>(m_height), static_cast<float>(m_depth)); }

    static Extent3D divideAndRoundDownClampTo1(Extent3D extent, u32 numerator)
    {
        ARKOSE_ASSERT(numerator > 0);
        u32 w = std::max(1u, extent.width() / 2);
        u32 h = std::max(1u, extent.height() / 2);
        u32 d = std::max(1u, extent.depth() / 2);
        return Extent3D(w, h, d);
    }

    template<class Archive>
    void serialize(Archive&);

private:
    uint32_t m_width {};
    uint32_t m_height {};
    uint32_t m_depth {};
};

namespace std {

    template<>
    struct hash<Extent2D> {
        std::size_t operator()(const Extent2D& extent) const
        {
            auto widthHash = std::hash<std::uint32_t>()(extent.width());
            auto heightHash = std::hash<std::uint32_t>()(extent.height());
            return hashCombine(widthHash, heightHash);
        }
    };

    template<>
    struct hash<Extent3D> {
        std::size_t operator()(const Extent3D& extent) const
        {
            auto widthHash = std::hash<std::uint32_t>()(extent.width());
            auto heightHash = std::hash<std::uint32_t>()(extent.height());
            auto depthHash = std::hash<std::uint32_t>()(extent.depth());
            return hashCombine(widthHash, hashCombine(heightHash, depthHash));
        }
    };

} // namespace std

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>

template<class Archive>
void Extent2D::serialize(Archive& archive)
{
    archive(cereal::make_nvp("width", m_width),
            cereal::make_nvp("height", m_height));
}

template<class Archive>
void Extent3D::serialize(Archive& archive)
{
    archive(cereal::make_nvp("width", m_width),
            cereal::make_nvp("height", m_height),
            cereal::make_nvp("depth", m_depth));
}

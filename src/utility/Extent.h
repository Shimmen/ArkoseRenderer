#pragma once

#include "utility/util.h"
#include "utility/Hash.h"
#include <cstdint>
#include <moos/vector.h>

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
        ASSERT(width >= 0);
        ASSERT(height >= 0);
    }
    constexpr Extent2D(const Extent2D& other)
        : Extent2D(other.m_width, other.m_height)
    {
    }

    [[nodiscard]] uint32_t width() const { return m_width; }
    [[nodiscard]] uint32_t height() const { return m_height; }

    bool operator!=(const Extent2D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent2D& other) const
    {
        return m_width == other.m_width && m_height == other.m_height;
    }

    moos::uvec2 asUIntVector() const { return moos::uvec2(m_width, m_height); }
    moos::ivec2 asIntVector() const { return moos::ivec2(m_width, m_height); }

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
    constexpr Extent3D(const Extent3D& other)
        : Extent3D(other.m_width, other.m_height, other.m_depth)
    {
    }
    constexpr Extent3D(const Extent2D& extent2d, uint32_t depth = 1)
        : Extent3D(extent2d.width(), extent2d.height(), depth)
    {
    }

    [[nodiscard]] uint32_t width() const { return m_width; }
    [[nodiscard]] uint32_t height() const { return m_height; }
    [[nodiscard]] uint32_t depth() const { return m_depth; }

    bool operator!=(const Extent3D& other) const
    {
        return !(*this == other);
    }
    bool operator==(const Extent3D& other) const
    {
        return m_width == other.m_width && m_height == other.m_height && m_depth == other.m_depth;
    }

    moos::uvec3 asUIntVector() const { return moos::uvec3(m_width, m_height, m_depth); }
    moos::ivec3 asIntVector() const { return moos::ivec3(m_width, m_height, m_depth); }

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
            auto widthHash = std::hash<uint32_t>()(extent.width());
            auto heightHash = std::hash<uint32_t>()(extent.height());
            return hashCombine(widthHash, heightHash);
        }
    };

    template<>
    struct hash<Extent3D> {
        std::size_t operator()(const Extent3D& extent) const
        {
            auto widthHash = std::hash<uint32_t>()(extent.width());
            auto heightHash = std::hash<uint32_t>()(extent.height());
            auto depthHash = std::hash<uint32_t>()(extent.depth());
            return hashCombine(widthHash, hashCombine(heightHash, depthHash));
        }
    };

} // namespace std

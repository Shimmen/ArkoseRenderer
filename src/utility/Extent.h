#pragma once

#include "utility/util.h"
#include <cstdint>

struct Extent2D {
    Extent2D()
        : Extent2D(0, 0)
    {
    }
    Extent2D(uint32_t width, uint32_t height)
        : m_width(width)
        , m_height(height)
    {
    }
    Extent2D(int width, int height)
        : m_width(width)
        , m_height(height)
    {
        ASSERT(width >= 0);
        ASSERT(height >= 0);
    }
    Extent2D(const Extent2D& other)
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

private:
    uint32_t m_width {};
    uint32_t m_height {};
};

struct Extent3D {
    Extent3D(uint32_t val = 0)
        : Extent3D(val, val, val)
    {
    }
    Extent3D(uint32_t width, uint32_t height, uint32_t depth)
        : m_width(width)
        , m_height(height)
        , m_depth(depth)
    {
    }
    Extent3D(const Extent3D& other)
        : Extent3D(other.m_width, other.m_height, other.m_depth)
    {
    }
    Extent3D(const Extent2D& extent2d)
        : Extent3D(extent2d.width(), extent2d.height(), 1)
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

private:
    uint32_t m_width {};
    uint32_t m_height {};
    uint32_t m_depth {};
};

#pragma once

#include "core/Assert.h"
#include "core/Types.h"

struct ClearColor {

    static ClearColor srgbColor(float r, float g, float b, float a = 1.0f)
    {
        return ClearColor(powf(r, 2.2f), powf(g, 2.2f), powf(b, 2.2f), a);
    }

    static ClearColor srgbColor(float rgb[3], float a = 1.0f)
    {
        return srgbColor(rgb[0], rgb[1], rgb[2], a);
    }

    static ClearColor dataValues(float r, float g, float b, float a)
    {
        return ClearColor(r, g, b, a);
    }

    static ClearColor black()
    {
        return ClearColor();
    }

    float r { 0.0f };
    float g { 0.0f };
    float b { 0.0f };
    float a { 0.0f };

private:
    ClearColor() = default;
    ClearColor(float r, float g, float b, float a)
        : r(r)
        , g(g)
        , b(b)
        , a(a)
    {
    }
};

enum class IndexType {
    UInt16,
    UInt32,
};

inline size_t sizeofIndexType(IndexType indexType)
{
    switch (indexType) {
    case IndexType::UInt16:
        return sizeof(uint16_t);
    case IndexType::UInt32:
        return sizeof(uint32_t);
    default:
        ASSERT_NOT_REACHED();
    }
}

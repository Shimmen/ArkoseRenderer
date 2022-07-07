#pragma once

#include "core/Assert.h"
#include "core/Types.h"

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

static constexpr const char* indexTypeToString(IndexType type)
{
    switch (type) {
    case IndexType::UInt16:
        return "UInt16";
    case IndexType::UInt32:
        return "UInt32";
    default:
        ASSERT_NOT_REACHED();
    }
}

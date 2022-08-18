#pragma once

#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/IndexType.h"

struct DrawCallDescription {

    enum class Type {
        Indexed,
        NonIndexed,
    };

    Type type;

    const Buffer* vertexBuffer;

    // for non-indexed draw calls
    uint32_t firstVertex { 0 };
    uint32_t vertexCount { 0 };

    // for indexed draw calls
    uint32_t firstIndex { 0 };
    uint32_t indexCount { 0 };
    int32_t vertexOffset { 0 };

    const Buffer* indexBuffer { nullptr };
    IndexType indexType { IndexType::UInt32 };

    // for instancing
    uint32_t instanceCount { 1 };
    uint32_t firstInstance { 0 };

    static DrawCallDescription makeSimple(Buffer& vertexBuffer, uint32_t vertexCount)
    {
        DrawCallDescription drawCall { .type = Type::NonIndexed,
                                       .vertexBuffer = &vertexBuffer,
                                       .vertexCount = vertexCount };
        return drawCall;
    }

    static DrawCallDescription makeSimpleIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, IndexType indexType = IndexType::UInt32)
    {
        DrawCallDescription drawCall { .type = Type::Indexed,
                                       .vertexBuffer = &vertexBuffer,
                                       .indexCount = indexCount,
                                       .indexBuffer = &indexBuffer,
                                       .indexType = indexType };
        return drawCall;
    }
};

#pragma once

#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/IndexType.h"

struct DrawCallDescription {

    enum class Type {
        Indexed,
        NonIndexed,
    };

    // (optional)
    const class Mesh* sourceMesh { nullptr };

    const Buffer* vertexBuffer;
    const Buffer* indexBuffer;

    Type type;
    uint32_t firstVertex { 0 };
    uint32_t firstIndex { 0 };

    uint32_t vertexCount { 0 };
    int32_t vertexOffset { 0 };

    IndexType indexType;
    uint32_t indexCount { 0 };

    uint32_t instanceCount { 1 };
    uint32_t firstInstance { 0 };

    static DrawCallDescription makeSimpleIndexed(Buffer& vertexBuffer, Buffer& indexBuffer, uint32_t indexCount, IndexType indexType = IndexType::UInt32)
    {
        DrawCallDescription drawCall { .vertexBuffer = &vertexBuffer,
                                       .indexBuffer = &indexBuffer,
                                       .type = Type::Indexed,
                                       .indexType = indexType,
                                       .indexCount = indexCount };
        return drawCall;
    }
};

#pragma once

#include "backend/base/Buffer.h"
#include "backend/util/Common.h"

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

    uint32_t vertexCount;
    int32_t vertexOffset;

    IndexType indexType;
    uint32_t indexCount;

    uint32_t instanceCount { 1 };
    uint32_t firstInstance { 0 };
};

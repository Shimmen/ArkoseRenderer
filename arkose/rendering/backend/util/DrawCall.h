#pragma once

#include "rendering/VertexAllocation.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/util/IndexType.h"

struct DrawCallDescription {

    enum class Type {
        Indexed,
        NonIndexed,
    };

    Type type;

    // for non-indexed draw calls
    uint32_t firstVertex { 0 };
    uint32_t vertexCount { 0 };

    // for indexed draw calls
    uint32_t firstIndex { 0 };
    uint32_t indexCount { 0 };
    int32_t vertexOffset { 0 };

    // for instancing
    uint32_t instanceCount { 1 };
    uint32_t firstInstance { 0 };

    static DrawCallDescription fromVertexAllocation(VertexAllocation const& vertexAllocation)
    {
        DrawCallDescription drawCall {};

        if (vertexAllocation.indexCount > 0) {
            drawCall.type = DrawCallDescription::Type::Indexed;
            drawCall.vertexOffset = vertexAllocation.firstVertex;
            drawCall.vertexCount = vertexAllocation.vertexCount;
            drawCall.firstIndex = vertexAllocation.firstIndex;
            drawCall.indexCount = vertexAllocation.indexCount;
        } else {
            drawCall.type = DrawCallDescription::Type::NonIndexed;
            drawCall.firstVertex = vertexAllocation.firstVertex;
            drawCall.vertexCount = vertexAllocation.vertexCount;
        }

        return drawCall;
    }
};

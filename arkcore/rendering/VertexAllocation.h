#pragma once

#include "core/Types.h"
#include <offsetAllocator/offsetAllocator.hpp>

struct VertexAllocation {
    u32 firstVertex { 0 };
    u32 vertexCount { 0 };
    u32 firstIndex { 0 };
    u32 indexCount { 0 };

    i32 firstSkinningVertex { -1 };
    bool hasSkinningData() const { return firstSkinningVertex >= 0; }

    std::vector<i32> firstMorphTargetVertices {};
    bool hasMorphTargetData() const { return firstMorphTargetVertices.size() > 0; }

    i32 firstVelocityVertex { -1 };
    bool hasVelocityData() const { return firstVelocityVertex >= 0; }

    bool isValid() const { return vertexCount > 0; }
    bool hasIndices() const { return indexCount > 0; }

    struct Internal {
        OffsetAllocator::Allocation vertexAlloc {};
        OffsetAllocator::Allocation indexAlloc {};
        OffsetAllocator::Allocation skinningVertAlloc {};
        OffsetAllocator::Allocation velocityVertAlloc {};
        std::vector<OffsetAllocator::Allocation> morphTargetVertAllocs {};
    };

    // For managing owned allocations to free when this is destroyed
    Internal internalAllocations;
};

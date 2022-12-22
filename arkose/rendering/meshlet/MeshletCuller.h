#pragma once

#include "core/Types.h"
#include "rendering/GpuScene.h"
#include "rendering/Registry.h"

class MeshletCuller final {
public:
    MeshletCuller();
    ~MeshletCuller();

    struct CullData {
        Buffer* indirectDrawCmd;
        Buffer* resultIndexBuffer;

    private:
        friend class MeshletCuller;

        Buffer* meshletRangeQueueBuffer;
        Buffer* triangleRangeQueueBuffer;

        Buffer* indirectDrawCmdBuffer;
        Buffer* miscDataBuffer;

        ComputeState* prepareIndirectDataState;
        BindingSet* prepareIndirectDataBindingSet;

        ComputeState* cullComputeState;
        BindingSet* cullBindingSet;
    };

    static constexpr u32 PostCullingMaxTriangleCount = 500'000;

    // NOTE: These must be power-of-two!
    static constexpr u32 MeshletRangeQueueSize = 16384;
    static constexpr u32 TriangleRangeQueueSize = 65536;

    static constexpr u32 WorkGroupSize = 64;
    static constexpr u32 WorkGroupCountForMaxUtilization = 2000; // TODO: Find a value which is good and valid!

    CullData& construct(GpuScene&, Registry&);
    void execute(CommandList&, GpuScene&, CullData const&) const;

private:
    // TODO: Move to some broker queue helper file perhaps?
    static Buffer& createBufferForBrokerQueue(Registry&, u32 queueItemCapacity);
    static void initializeBrokerQueue(CommandList&, Buffer& brokerQueueBuffer);
};

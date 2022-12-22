#include "MeshletCuller.h"

// Shared shader headers
#include "IndirectData.h"

MeshletCuller::MeshletCuller() = default;
MeshletCuller::~MeshletCuller() = default;

MeshletCuller::CullData& MeshletCuller::construct(GpuScene& scene, Registry& reg)
{
    std::vector<ShaderDefine> defines = { ShaderDefine::makeInt("MESHLET_QUEUE_SIZE", MeshletRangeQueueSize),
                                          ShaderDefine::makeInt("TRIANGLE_QUEUE_SIZE", TriangleRangeQueueSize),
                                          ShaderDefine::makeInt("WORK_GROUP_SIZE", WorkGroupSize),
                                          ShaderDefine::makeInt("NUM_WORK_GROUPS", WorkGroupCountForMaxUtilization) };
    Shader shader = Shader::createCompute("meshlet/culling.comp", defines);

    // TODO: Maybe pass in the result index buffer to this construct function?
    Buffer& triangleResultIndexBuffer = reg.createBuffer(2 * 3 * sizeof(u32) * PostCullingMaxTriangleCount, Buffer::Usage::Index, Buffer::MemoryHint::GpuOnly);
    triangleResultIndexBuffer.setName("MeshletPostCullIndexBuffer");

    Buffer& meshletRangeQueueBuffer = createBufferForBrokerQueue(reg, MeshletRangeQueueSize);
    meshletRangeQueueBuffer.setName("MeshletRangeQueueBuffer");
    Buffer& triangleRangeQueueBuffer = createBufferForBrokerQueue(reg, TriangleRangeQueueSize);
    triangleRangeQueueBuffer.setName("TriangleRangeQueueBuffer");

    Buffer& indirectDrawCmdBuffer = reg.createBuffer(sizeof(IndexedDrawCmd), Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    indirectDrawCmdBuffer.setName("MeshletIndirectDrawCmdBuffer");

    Buffer& miscDataBuffer = reg.createBuffer(4 * sizeof(u32), Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);
    miscDataBuffer.setName("MeshletMiscDataBuffer");

    BindingSet& bindingSet = reg.createBindingSet({ ShaderBinding::storageBufferReadonly(*reg.getBuffer("SceneObjectData"), ShaderStage::Compute),
                                                    ShaderBinding::storageBufferReadonly(scene.meshletManager().meshletBuffer(), ShaderStage::Compute),
                                                    ShaderBinding::storageBufferReadonly(scene.meshletManager().meshletIndexBuffer(), ShaderStage::Compute),
                                                    ShaderBinding::storageBuffer(meshletRangeQueueBuffer, ShaderStage::Compute),
                                                    ShaderBinding::storageBuffer(triangleRangeQueueBuffer, ShaderStage::Compute),
                                                    ShaderBinding::storageBuffer(triangleResultIndexBuffer, ShaderStage::Compute),
                                                    ShaderBinding::storageBuffer(indirectDrawCmdBuffer, ShaderStage::Compute),
                                                    ShaderBinding::storageBuffer(miscDataBuffer, ShaderStage::Compute) });

    ComputeState& computeState = reg.createComputeState(shader, { &bindingSet });
    computeState.setName("MeshletCullState");

    BindingSet& prepareIndirectDataBindingSet = reg.createBindingSet({ ShaderBinding::storageBuffer(indirectDrawCmdBuffer, ShaderStage::Compute) });
    ComputeState& prepareIndirectDataState = reg.createComputeState(Shader::createCompute("meshlet/prepareIndirectArgs.comp"), { &prepareIndirectDataBindingSet });

    CullData& cullData = reg.allocate<CullData>();
    cullData.indirectDrawCmd = &indirectDrawCmdBuffer;
    cullData.resultIndexBuffer = &triangleResultIndexBuffer;
    cullData.meshletRangeQueueBuffer = &meshletRangeQueueBuffer;
    cullData.triangleRangeQueueBuffer = &triangleRangeQueueBuffer;
    cullData.indirectDrawCmdBuffer = &indirectDrawCmdBuffer;
    cullData.miscDataBuffer = &miscDataBuffer;
    cullData.prepareIndirectDataState = &prepareIndirectDataState;
    cullData.prepareIndirectDataBindingSet = &prepareIndirectDataBindingSet;
    cullData.cullComputeState = &computeState;
    cullData.cullBindingSet = &bindingSet;

    return cullData;
}

void MeshletCuller::execute(CommandList& cmdList, GpuScene& scene, CullData const& cullData) const
{
    ////////////////////////////////////////////////////////////////////////////
    // Initialize data before initiating culling

    cmdList.fillBuffer(*cullData.miscDataBuffer, 0);
    initializeBrokerQueue(cmdList, *cullData.meshletRangeQueueBuffer);
    initializeBrokerQueue(cmdList, *cullData.triangleRangeQueueBuffer);

    cmdList.setComputeState(*cullData.prepareIndirectDataState);
    cmdList.bindSet(*cullData.prepareIndirectDataBindingSet, 0);
    cmdList.dispatch(1, 1, 1);

    cmdList.bufferWriteBarrier({ cullData.indirectDrawCmdBuffer });

    ////////////////////////////////////////////////////////////////////////////
    // Execute culling

    // TODO: Consider this naming scheme..
    u32 instanceCount = narrow_cast<u32>(scene.drawableCountForFrame());

    cmdList.setComputeState(*cullData.cullComputeState);
    cmdList.bindSet(*cullData.cullBindingSet, 0);
    cmdList.setNamedUniform<u32>("instanceCount", instanceCount);
    cmdList.setNamedUniform<u32>("maxTriangleCount", PostCullingMaxTriangleCount); // NOTE: We can dynamically reduce this for testing!

    cmdList.dispatch(WorkGroupCountForMaxUtilization, 1, 1);

    cmdList.bufferWriteBarrier({ cullData.resultIndexBuffer, cullData.indirectDrawCmdBuffer });
    cmdList.bufferWriteBarrier({ cullData.miscDataBuffer }); // Just for debugging purposes
}

Buffer& MeshletCuller::createBufferForBrokerQueue(Registry& reg, u32 queueItemCapacity)
{
    size_t ringBufferSize = queueItemCapacity * (4 * sizeof(u32)); // must always use uvec4 due to array padding rules
    size_t ticketBufferSize = queueItemCapacity * sizeof(u32);

    size_t totalSize = sizeof(u64) + sizeof(i32) + sizeof(i32) + ticketBufferSize + ringBufferSize; // todo: right size? We use scalar layout?

    Buffer& buffer = reg.createBuffer(totalSize, Buffer::Usage::StorageBuffer, Buffer::MemoryHint::GpuOnly);

    return buffer;
}

void MeshletCuller::initializeBrokerQueue(CommandList& cmdList, Buffer& brokerQueueBuffer)
{
    // Fill the entire buffer range with zero, as that's the initialized state of the buffer (see brokerQueue.glsl)
    cmdList.fillBuffer(brokerQueueBuffer, 0u);
}

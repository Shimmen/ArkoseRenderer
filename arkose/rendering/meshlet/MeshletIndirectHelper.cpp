#include "MeshletIndirectHelper.h"

#include "rendering/GpuScene.h"
#include "rendering/Registry.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/util/ScopedDebugZone.h"

Buffer& MeshletIndirectHelper::createIndirectBuffer(Registry& reg, u32 maxMeshletCount)
{
    constexpr size_t countSizeWithPadding = sizeof(ark::uvec4);
    size_t bufferSize = maxMeshletCount * sizeof(ark::uvec4) + countSizeWithPadding;
    return reg.createBuffer(bufferSize, Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
}

MeshletIndirectSetupState const& MeshletIndirectHelper::createMeshletIndirectSetupState(Registry& reg, std::vector<Buffer*> const& indirectBuffers)
{
    // Construct the indirect buffers array from the passed in indirect buffers, and pad out remaining slots
    std::array<Buffer*, IndirectBufferCount> indirectBuffersArray {};

    u32 bufferIdx = 0;
    ARKOSE_ASSERT(indirectBuffers.size() > 0);
    for (; bufferIdx < indirectBuffers.size(); ++bufferIdx) {
        ARKOSE_ASSERT(indirectBuffers[bufferIdx] != nullptr);
        indirectBuffersArray[bufferIdx] = indirectBuffers[bufferIdx];
    }
    for (; bufferIdx < IndirectBufferCount; ++bufferIdx) {
        indirectBuffersArray[bufferIdx] = indirectBuffersArray[0];
    }

    MeshletIndirectSetupState& state = reg.allocate<MeshletIndirectSetupState>();
    state.indirectBuffers = indirectBuffers;

    state.cameraBindingSet = reg.getBindingSet("SceneCameraSet");
    state.indirectDataBindingSet = &reg.createBindingSet({ ShaderBinding::storageBuffer(*reg.getBuffer("SceneObjectData")),
                                                           ShaderBinding::storageBuffer(*indirectBuffersArray[0]) /*,
                                                           ShaderBinding::storageBuffer(*state.indirectBuffers[1]),
                                                           ShaderBinding::storageBuffer(*state.indirectBuffers[2]),
                                                           ShaderBinding::storageBuffer(*state.indirectBuffers[3])*/ });

    auto meshletTaskSetupDefines = { ShaderDefine::makeInt("GROUP_SIZE", GroupSize) };
    Shader meshletTaskSetupShader = Shader::createCompute("meshlet/meshletTaskSetup.comp", meshletTaskSetupDefines);
    state.taskSetupComputeState = &reg.createComputeState(meshletTaskSetupShader, { state.cameraBindingSet, state.indirectDataBindingSet });

    return state;
}

void MeshletIndirectHelper::executeMeshletIndirectSetup(GpuScene& scene, CommandList& cmdList, UploadBuffer& uploadBuffer,
                                                       MeshletIndirectSetupState const& state, MeshletIndirectSetupOptions const& options) const
{
    ScopedDebugZone zone { cmdList, "Meshlet task setup" };

    // Set first u32 (i.e. indirect count) to 0 before using it for accumulation in the shader
    for (Buffer* indirectBuffer : state.indirectBuffers) {
        uploadBuffer.upload(0u, *indirectBuffer, 0);
    }
    cmdList.executeBufferCopyOperations(uploadBuffer);

    cmdList.setComputeState(*state.taskSetupComputeState);
    cmdList.bindSet(*state.cameraBindingSet, 0);
    cmdList.bindSet(*state.indirectDataBindingSet, 1);

    u32 drawableCount = scene.drawableCountForFrame();
    cmdList.setNamedUniform("drawableCount", drawableCount);

    // Set options
    cmdList.setNamedUniform("frustumCull", options.frustumCullInstances);

    cmdList.dispatch({ drawableCount, 1, 1 }, { GroupSize, 1, 1 });
    cmdList.bufferWriteBarrier(state.indirectBuffers);
}

void MeshletIndirectHelper::drawMeshletsWithIndirectBuffer(CommandList& cmdList, Buffer const& indirectBuffer) const
{
    // The indirect count is the first u32 in the indirect buffer, padded out to a whole uvec4.
    constexpr u32 countDataOffset = 0;

    // Indirect command data start at the next uvec4 after the count, with a stride of uvec4.
    // The w-component of the uvec4 is the "drawable lookup" which is metadata.
    constexpr u32 cmdDataStride = sizeof(ark::uvec4);
    constexpr u32 cmdDataOffset = sizeof(ark::uvec4);

    cmdList.drawMeshTasksIndirect(indirectBuffer, cmdDataStride, cmdDataOffset,
                                  indirectBuffer, countDataOffset);
}

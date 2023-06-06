#include "MeshletIndirectHelper.h"

#include "rendering/GpuScene.h"
#include "rendering/Registry.h"
#include "rendering/backend/util/UploadBuffer.h"
#include "rendering/backend/base/Buffer.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/util/ScopedDebugZone.h"

MeshletIndirectBuffer& MeshletIndirectHelper::createIndirectBuffer(Registry& reg, DrawKey drawKeyMask, u32 maxMeshletCount) const
{
    constexpr size_t countSizeWithPadding = sizeof(uvec4);
    size_t bufferSize = maxMeshletCount * sizeof(uvec4) + countSizeWithPadding;

    auto& indirectBuffer = reg.allocate<MeshletIndirectBuffer>();
    indirectBuffer.buffer = &reg.createBuffer(bufferSize, Buffer::Usage::IndirectBuffer, Buffer::MemoryHint::GpuOnly);
    indirectBuffer.drawKeyMask = drawKeyMask;
    return indirectBuffer;
}

MeshletIndirectSetupState const& MeshletIndirectHelper::createMeshletIndirectSetupState(Registry& reg, std::vector<MeshletIndirectBuffer*> const& indirectBuffers) const
{
    auto meshletTaskSetupDefines = { ShaderDefine::makeInt("GROUP_SIZE", GroupSize) };
    Shader meshletTaskSetupShader = Shader::createCompute("meshlet/meshletTaskSetup.comp", meshletTaskSetupDefines);

    MeshletIndirectSetupState& state = reg.allocate<MeshletIndirectSetupState>();
    state.cameraBindingSet = reg.getBindingSet("SceneCameraSet");

    for (MeshletIndirectBuffer* indirectBuffer : indirectBuffers) {

        ARKOSE_ASSERT(indirectBuffer != nullptr);
        state.indirectBuffers.emplace_back(indirectBuffer);
        state.rawIndirectBuffers.emplace_back(indirectBuffer->buffer);

        MeshletIndirectSetupDispatch& dispatch = state.dispatches.emplace_back();
        dispatch.drawKeyMask = indirectBuffer->drawKeyMask;
        dispatch.indirectDataBindingSet = &reg.createBindingSet({ ShaderBinding::storageBuffer(*reg.getBuffer("SceneObjectData")),
                                                                  ShaderBinding::storageBuffer(*indirectBuffer->buffer) });
        dispatch.taskSetupComputeState = &reg.createComputeState(meshletTaskSetupShader, { state.cameraBindingSet, dispatch.indirectDataBindingSet });
    }

    return state;
}

void MeshletIndirectHelper::executeMeshletIndirectSetup(GpuScene& scene, CommandList& cmdList, UploadBuffer& uploadBuffer,
                                                        MeshletIndirectSetupState const& state, MeshletIndirectSetupOptions const& options) const
{
    ScopedDebugZone zone { cmdList, "Meshlet task setup" };

    // Set first u32 (i.e. indirect count) to 0 before using it for accumulation in the shader
    for (Buffer* indirectBuffer : state.rawIndirectBuffers) {
        uploadBuffer.upload(0u, *indirectBuffer, 0);
    }
    cmdList.executeBufferCopyOperations(uploadBuffer);

    const u32 drawableCount = scene.drawableCountForFrame();

    for (MeshletIndirectSetupDispatch const& dispatch : state.dispatches) {
        cmdList.setComputeState(*dispatch.taskSetupComputeState);
        cmdList.bindSet(*state.cameraBindingSet, 0);
        cmdList.bindSet(*dispatch.indirectDataBindingSet, 1);

        cmdList.setNamedUniform("drawableCount", drawableCount);
        cmdList.setNamedUniform("drawKeyMask", dispatch.drawKeyMask.asUint32());

        // Set options
        cmdList.setNamedUniform("frustumCull", options.frustumCullInstances);

        cmdList.dispatch({ drawableCount, 1, 1 }, { GroupSize, 1, 1 });
    }

    cmdList.bufferWriteBarrier(state.rawIndirectBuffers);
}

void MeshletIndirectHelper::drawMeshletsWithIndirectBuffer(CommandList& cmdList, MeshletIndirectBuffer const& indirectBuffer) const
{
    // The indirect count is the first u32 in the indirect buffer, padded out to a whole uvec4.
    constexpr u32 countDataOffset = 0;

    // Indirect command data start at the next uvec4 after the count, with a stride of uvec4.
    // The w-component of the uvec4 is the "drawable lookup" which is metadata.
    constexpr u32 cmdDataStride = sizeof(uvec4);
    constexpr u32 cmdDataOffset = sizeof(uvec4);

    cmdList.drawMeshTasksIndirect(*indirectBuffer.buffer, cmdDataStride, cmdDataOffset,
                                  *indirectBuffer.buffer, countDataOffset);
}

#pragma once

#include "core/NonCopyable.h"
#include "core/Types.h"
#include "rendering/DrawKey.h"
#include <vector>

class BindingSet;
class Buffer;
class CommandList;
class ComputeState;
class GpuScene;
class Registry;
class UploadBuffer;

struct MeshletIndirectBuffer {
    Buffer* buffer { nullptr };
    DrawKey drawKeyMask {};
};

struct MeshletIndirectSetupDispatch {
    DrawKey drawKeyMask {};
    ComputeState* taskSetupComputeState { nullptr };
    BindingSet* indirectDataBindingSet { nullptr };
};

struct MeshletIndirectSetupState {
    std::vector<MeshletIndirectBuffer*> indirectBuffers {};
    std::vector<Buffer*> rawIndirectBuffers {};

    BindingSet* cameraBindingSet { nullptr };
    std::vector<MeshletIndirectSetupDispatch> dispatches {};
};

struct MeshletIndirectSetupOptions {
    bool frustumCullInstances { false };
};

class MeshletIndirectHelper {
public:
    MeshletIndirectHelper() = default;
    ~MeshletIndirectHelper() = default;
    NON_COPYABLE(MeshletIndirectHelper)

    // Create a buffer used to store encoded indirect mesh task draw commands & required meta data
    MeshletIndirectBuffer& createIndirectBuffer(Registry&, DrawKey drawKeyMask, u32 maxMeshletCount) const;

    // Create the state needed for meshlet task setup execution
    MeshletIndirectSetupState const& createMeshletIndirectSetupState(Registry&, std::vector<MeshletIndirectBuffer*> const& indirectBuffers) const;

    // Execute the meshlet task setup, from the given state
    void executeMeshletIndirectSetup(GpuScene&, CommandList&, UploadBuffer&, MeshletIndirectSetupState const&, MeshletIndirectSetupOptions const&) const;

    // Draws meshlets with an indirect buffer (created from `createIndirectBuffer`)
    // Note that the indirect buffer needs to be filled in with data to be usable here.
    void drawMeshletsWithIndirectBuffer(CommandList&, MeshletIndirectBuffer const&) const;

private:

    // Number of indirect buffers we can process in a single call to `executeMeshletTaskSetup`
    static constexpr u32 IndirectBufferCount = 1;

    // Group size for compute dispatches
    static constexpr u32 GroupSize = 32;

};

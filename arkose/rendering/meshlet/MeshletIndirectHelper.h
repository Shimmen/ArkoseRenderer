#pragma once

#include "core/NonCopyable.h"
#include "core/Types.h"
#include <vector>

class BindingSet;
class Buffer;
class CommandList;
class ComputeState;
class GpuScene;
class Registry;
class UploadBuffer;

struct MeshletIndirectSetupState {
    BindingSet* cameraBindingSet { nullptr };
    BindingSet* indirectDataBindingSet { nullptr };
    ComputeState* taskSetupComputeState { nullptr };
    std::vector<Buffer*> indirectBuffers {};
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
    Buffer& createIndirectBuffer(Registry&, u32 maxMeshletCount) const;

    // Create the state needed for meshlet task setup execution
    MeshletIndirectSetupState const& createMeshletIndirectSetupState(Registry&, std::vector<Buffer*> const& indirectBuffers) const;

    // Execute the meshlet task setup, from the given state
    void executeMeshletIndirectSetup(GpuScene&, CommandList&, UploadBuffer&, MeshletIndirectSetupState const&, MeshletIndirectSetupOptions const&) const;

    // Draws meshlets with an indirect buffer (created from `createIndirectBuffer`)
    // Note that the indirect buffer needs to be filled in with data to be usable here.
    void drawMeshletsWithIndirectBuffer(CommandList&, Buffer const& indirectBuffer) const;

private:

    // Number of indirect buffers we can process in a single call to `executeMeshletTaskSetup`
    static constexpr u32 IndirectBufferCount = 1;

    // Group size for compute dispatches
    static constexpr u32 GroupSize = 32;

};

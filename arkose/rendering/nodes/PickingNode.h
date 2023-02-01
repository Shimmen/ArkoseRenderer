#pragma once

#include "rendering/RenderPipelineNode.h"

class PickingNode final : public RenderPipelineNode {
public:

    std::string name() const override { return "Picking"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    struct DeferredResult {
        Buffer* resultBuffer {};

        // What should we use the result for?
        bool selectMesh { false };
        bool specifyFocusDepth { false };
    };

    std::optional<DeferredResult> m_pendingDeferredResult {};

    void processDeferredResult(CommandList& cmdList, GpuScene&, const DeferredResult&);
    void setFocusDepth(GpuScene&, float focusDepth);
};

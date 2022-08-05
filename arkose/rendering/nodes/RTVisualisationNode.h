#pragma once

#include "rendering/RenderPipelineNode.h"

class RTVisualisationNode final : public RenderPipelineNode {
public:

    enum class Mode {
        FirstHit,
        DirectLight,
    };

    RTVisualisationNode(Mode);

    std::string name() const override { return "RT visualisation"; }
    ExecuteCallback construct(GpuScene&, Registry&) override;

private:

    Mode m_mode;

};

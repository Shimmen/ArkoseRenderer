#pragma once

#include "rendering/RenderPipelineNode.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"

class SSAONode final : public RenderPipelineNode {
public:
    explicit SSAONode(Scene&);

    std::string name() const override { return "SSAO"; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    Scene& m_scene;

    int m_kernelSampleCount { 0 };
    Buffer* m_kernelSampleBuffer {};
    std::vector<vec4> generateKernel(int numSamples) const;
};

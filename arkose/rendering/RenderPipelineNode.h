#pragma once

#include "core/Badge.h"
#include "rendering/AppState.h"
#include "rendering/Registry.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/backend/Resources.h"
#include "utility/AvgElapsedTimer.h"
#include <functional>
#include <memory>
#include <string>

class GpuScene;
class RenderPipeline;
class Texture;
class UploadBuffer;

class RenderPipelineNode {
public:
    RenderPipelineNode() = default;
    virtual ~RenderPipelineNode() = default;

    using ExecuteCallback = std::function<void(const AppState&, CommandList&, UploadBuffer&)>;

    // An execute callback that does nothing. Useful for early exit when nothing to execute.
    static const ExecuteCallback NullExecuteCallback;

    [[nodiscard]] AvgElapsedTimer& timer() { return m_timer; }

    [[nodiscard]] virtual std::string name() const = 0;

    virtual UpscalingTech upscalingTech() const { return UpscalingTech::None; }
    virtual UpscalingQuality upscalingQuality() const { return UpscalingQuality::Balanced; }
    virtual bool isUpscalingNode() const final { return upscalingTech() != UpscalingTech::None; }

    virtual ExecuteCallback construct(GpuScene&, Registry&) = 0;

    // Draw GUI for this node
    virtual void drawGui() {};

    void drawTextureVisualizeGui(Texture&);

    void setPipeline(Badge<RenderPipeline>, RenderPipeline& owningPipeline) { m_owningPipeline = &owningPipeline; }
    RenderPipeline& pipeline() const { return *m_owningPipeline; }

private:
    AvgElapsedTimer m_timer;
    RenderPipeline* m_owningPipeline { nullptr };

    // For tracking gui drawing state
    std::unordered_map<Texture*, bool> m_textureVisualizers {};
};

class RenderPipelineLambdaNode final : public RenderPipelineNode {
public:
    using ConstructorFunction = std::function<ExecuteCallback(GpuScene&, Registry&)>;
    RenderPipelineLambdaNode(std::string name, ConstructorFunction);

    std::string name() const override { return m_name; }

    ExecuteCallback construct(GpuScene&, Registry&) override;

private:
    std::string m_name;
    ConstructorFunction m_constructorFunction;
};

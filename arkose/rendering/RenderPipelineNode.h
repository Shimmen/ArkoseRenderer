#pragma once

#include "AppState.h"
#include "Registry.h"
#include "rendering/backend/base/CommandList.h"
#include "rendering/backend/Resources.h"
#include "utility/AvgElapsedTimer.h"
#include <functional>
#include <memory>
#include <string>

class GpuScene;
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

    virtual ExecuteCallback construct(GpuScene&, Registry&) = 0;

    // Draw GUI for this node
    virtual void drawGui() {};

private:
    AvgElapsedTimer m_timer;
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

#pragma once

#include "AppState.h"
#include "Registry.h"
#include "backend/base/CommandList.h"
#include "backend/Resources.h"
#include "utility/AvgAccumulator.h"
#include <functional>
#include <memory>
#include <string>

class NodeTimer {
public:
    void reportCpuTime(double);
    double averageCpuTime() const;

    void reportGpuTime(double);
    double averageGpuTime() const;

private:
    AvgAccumulator<double, 60> m_cpuAccumulator;
    AvgAccumulator<double, 60> m_gpuAccumulator;
};

class RenderPipelineNode {
public:
    RenderPipelineNode() = default;
    virtual ~RenderPipelineNode() = default;

    using ExecuteCallback = std::function<void(const AppState&, CommandList&)>;

    // An execute callback that does nothing. Useful for early exit when nothing to execute.
    static const ExecuteCallback NullExecuteCallback;

    [[nodiscard]] NodeTimer& timer();

    [[nodiscard]] virtual std::string name() const = 0;

    //! This is not const since we need to write to members here that are shared for the whole node.
    virtual void constructNode(Registry&) {};

    //! This is const, since changing or writing to any members would probably break stuff
    //! since this is called n times, one for each frame at reconstruction.
    virtual ExecuteCallback constructFrame(Registry&) const { return RenderPipelineNode::ExecuteCallback(); };

private:
    NodeTimer m_timer;
};

class RenderPipelineBasicNode final : public RenderPipelineNode {
public:
    using ConstructorFunction = std::function<ExecuteCallback(Registry&)>;
    RenderPipelineBasicNode(std::string name, ConstructorFunction);

    std::string name() const override { return m_name; }

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    std::string m_name;
    ConstructorFunction m_constructorFunction;
};

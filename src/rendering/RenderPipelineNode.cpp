#include "RenderPipelineNode.h"

#include <utility>

const RenderPipelineNode::ExecuteCallback RenderPipelineNode::NullExecuteCallback = [&](const AppState&, CommandList&) {};

void NodeTimer::reportCpuTime(double time)
{
    m_cpuAccumulator.report(time);
}

double NodeTimer::averageCpuTime() const
{
    return m_cpuAccumulator.runningAverage();
}

void NodeTimer::reportGpuTime(double time)
{
    m_gpuAccumulator.report(time);
}

double NodeTimer::averageGpuTime() const
{
    return m_gpuAccumulator.runningAverage();
}

RenderPipelineNode::RenderPipelineNode(std::string name)
    : m_name(std::move(name))
{
}

const std::string& RenderPipelineNode::name() const
{
    return m_name;
}

NodeTimer& RenderPipelineNode::timer()
{
    return m_timer;
}

RenderPipelineBasicNode::RenderPipelineBasicNode(std::string name, ConstructorFunction constructorFunction)
    : RenderPipelineNode(std::move(name))
    , m_constructorFunction(std::move(constructorFunction))
{
}

void RenderPipelineBasicNode::constructNode(Registry&)
{
    // Intentionally empty. If you want to have node resource, create a custom RenderPipelineNode subclass.
}

RenderPipelineBasicNode::ExecuteCallback RenderPipelineBasicNode::constructFrame(Registry& frameManager) const
{
    return m_constructorFunction(frameManager);
}

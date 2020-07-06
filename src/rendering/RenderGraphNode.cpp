#include "RenderGraphNode.h"

#include <utility>

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

RenderGraphNode::RenderGraphNode(std::string name)
    : m_name(std::move(name))
{
}

const std::string& RenderGraphNode::name() const
{
    return m_name;
}

NodeTimer& RenderGraphNode::timer()
{
    return m_timer;
}

RenderGraphBasicNode::RenderGraphBasicNode(std::string name, ConstructorFunction constructorFunction)
    : RenderGraphNode(std::move(name))
    , m_constructorFunction(std::move(constructorFunction))
{
}

void RenderGraphBasicNode::constructNode(Registry&)
{
    // Intentionally empty. If you want to have node resource, create a custom RenderGraphNode subclass.
}

RenderGraphBasicNode::ExecuteCallback RenderGraphBasicNode::constructFrame(Registry& frameManager) const
{
    return m_constructorFunction(frameManager);
}

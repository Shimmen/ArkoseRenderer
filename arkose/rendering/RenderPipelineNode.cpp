#include "RenderPipelineNode.h"

#include <utility>

const RenderPipelineNode::ExecuteCallback RenderPipelineNode::NullExecuteCallback = [&](const AppState&, CommandList&, UploadBuffer&) {};

RenderPipelineLambdaNode::RenderPipelineLambdaNode(std::string name, ConstructorFunction constructorFunction)
    : m_name(std::move(name))
    , m_constructorFunction(std::move(constructorFunction))
{
}

RenderPipelineNode::ExecuteCallback RenderPipelineLambdaNode::construct(GpuScene& scene, Registry& reg)
{
    return m_constructorFunction(scene, reg);
}

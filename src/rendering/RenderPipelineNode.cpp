#include "RenderPipelineNode.h"

#include <utility>

const RenderPipelineNode::ExecuteCallback RenderPipelineNode::NullExecuteCallback = [&](const AppState&, CommandList&, UploadBuffer&) {};

RenderPipelineBasicNode::RenderPipelineBasicNode(std::string name, ConstructorFunction constructorFunction)
    : RenderPipelineNode()
    , m_name(std::move(name))
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

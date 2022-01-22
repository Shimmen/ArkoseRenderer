#include "RenderPipelineNode.h"

#include <utility>

const RenderPipelineNode::ExecuteCallback RenderPipelineNode::NullExecuteCallback = [&](const AppState&, CommandList&, UploadBuffer&) {};

RenderPipelineBasicNode::RenderPipelineBasicNode(std::string name, ConstructorFunction constructorFunction)
    : RenderPipelineNode()
    , m_name(std::move(name))
    , m_constructorFunction(std::move(constructorFunction))
{
}

RenderPipelineBasicNode::ExecuteCallback RenderPipelineBasicNode::construct(Registry& reg)
{
    return m_constructorFunction(reg);
}

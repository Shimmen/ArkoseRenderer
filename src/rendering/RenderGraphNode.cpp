#include "RenderGraphNode.h"

#include <utility>

RenderGraphNode::RenderGraphNode(std::string name)
    : m_name(std::move(name))
{
}

const std::string& RenderGraphNode::name() const
{
    return m_name;
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

#include "RenderGraph.h"

#include <utility/Logging.h>

void RenderGraph::addNode(const std::string& name, RenderGraphBasicNode::ConstructorFunction constructorFunction)
{
    // All nodes should be added before construction!
    ASSERT(m_frameContexts.empty());

    auto node = std::make_unique<RenderGraphBasicNode>(name, constructorFunction);
    m_allNodes.emplace_back(std::move(node));
}

void RenderGraph::addNode(std::unique_ptr<RenderGraphNode>&& node)
{
    m_allNodes.emplace_back(std::move(node));
}

void RenderGraph::constructAll(Registry& nodeManager, std::vector<Registry*> frameManagers)
{
    m_frameContexts.clear();

    // TODO: For debugability it would be nice if the frame resources were constructed right after the node resources, for each node

    for (auto& node : m_allNodes) {
        nodeManager.setCurrentNode(node->name());
        node->constructNode(nodeManager);
    }

    for (auto& frameManager : frameManagers) {
        FrameContext frameCtx {};

        for (auto& node : m_allNodes) {
            frameManager->setCurrentNode(node->name());
            auto executeCallback = node->constructFrame(*frameManager);
            frameCtx.nodeContexts.push_back({ .node = node.get(),
                                              .executeCallback = executeCallback });
        }

        m_frameContexts[frameManager] = frameCtx;
    }

    nodeManager.setCurrentNode("-");
    for (auto& frameManager : frameManagers) {
        frameManager->setCurrentNode("-");
    }
}

void RenderGraph::forEachNodeInResolvedOrder(const Registry& frameManager, std::function<void(std::string nodeName, NodeTimer& timer, const RenderGraphNode::ExecuteCallback&)> callback) const
{
    auto entry = m_frameContexts.find(&frameManager);
    ASSERT(entry != m_frameContexts.end());

    // TODO: We also have to make sure that nodes rendering to the screen are last (and in some respective order that makes sense)
    // TODO: Actually run the callback in the correctly resolved order!

    const FrameContext& frameContext = entry->second;
    for (auto& [node, execCallback] : frameContext.nodeContexts) {
        std::string nodeDisplayName = node->displayName().value_or(node->name());
        callback(nodeDisplayName, node->timer(), execCallback);
    }
}

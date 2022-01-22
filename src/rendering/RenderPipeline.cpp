#include "RenderPipeline.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <fmt/format.h>

void RenderPipeline::addNode(const std::string& name, RenderPipelineBasicNode::ConstructorFunction constructorFunction)
{
    // All nodes should be added before construction!
    ASSERT(m_nodeContexts.empty());

    auto node = std::make_unique<RenderPipelineBasicNode>(name, constructorFunction);
    m_allNodes.emplace_back(std::move(node));
}

void RenderPipeline::addNode(std::unique_ptr<RenderPipelineNode>&& node)
{
    m_allNodes.emplace_back(std::move(node));
}

void RenderPipeline::constructAll(Registry& registry)
{
    SCOPED_PROFILE_ZONE();

    // TODO: This is slightly confusing.. why not make this "destruction" more explicit?
    m_nodeContexts.clear();

    LogInfo("Constructing node resources:\n");
    for (auto& node : m_allNodes) {

        SCOPED_PROFILE_ZONE_DYNAMIC(node->name(), 0x252515)
        LogInfo("  %s\n", node->name().c_str());

        registry.setCurrentNode(node->name());

        // TODO: Remove the constructNode variant..
        node->constructNode(registry);
        auto executeCallback = node->constructFrame(registry);

        m_nodeContexts.push_back({ .node = node.get(),
                                    .executeCallback = executeCallback });
    }

    // Is this useful? Also, maybe use optional instead?
    registry.setCurrentNode("-");
}

void RenderPipeline::forEachNodeInResolvedOrder(const Registry& frameManager, std::function<void(std::string nodeName, AvgElapsedTimer& timer, const RenderPipelineNode::ExecuteCallback&)> callback) const
{
    // TODO: Actually run the callback in the correctly resolved order!
    // TODO: We also have to make sure that nodes rendering to the screen are last (and in some respective order that makes sense)

    ASSERT(m_nodeContexts.size() > 0);

    for (auto& [node, execCallback] : m_nodeContexts) {
        callback(node->name(), node->timer(), execCallback);
    }
}

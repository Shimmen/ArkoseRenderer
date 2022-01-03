#include "RenderPipeline.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <fmt/format.h>

void RenderPipeline::addNode(const std::string& name, RenderPipelineBasicNode::ConstructorFunction constructorFunction)
{
    // All nodes should be added before construction!
    ASSERT(m_frameContexts.empty());

    auto node = std::make_unique<RenderPipelineBasicNode>(name, constructorFunction);
    m_allNodes.emplace_back(std::move(node));
}

void RenderPipeline::addNode(std::unique_ptr<RenderPipelineNode>&& node)
{
    m_allNodes.emplace_back(std::move(node));
}

void RenderPipeline::constructAll(Registry& nodeRegistry, std::vector<Registry*> frameRegistries)
{
    SCOPED_PROFILE_ZONE();

    m_frameContexts.clear();

    // TODO: For debugability it would be nice if the frame resources were constructed right after the node resources, for each node

    LogInfo("Constructing per-node stuff:\n");
    int nextNodeIdx = 1;

    {
        SCOPED_PROFILE_ZONE_NAMED("Node resources");
        for (auto& node : m_allNodes) {
            LogInfo("  node=%i (%s)\n", nextNodeIdx++, node->name().c_str());
            nodeRegistry.setCurrentNode(node->name());
            node->constructNode(nodeRegistry);
        }
    }

    LogInfo("Constructing per-frame stuff:\n");
    int nextFrameIdx = 1;

    {
        SCOPED_PROFILE_ZONE_NAMED("Frame resources");
        for (auto& frameRegistry : frameRegistries) {
            SCOPED_PROFILE_ZONE_DYNAMIC(fmt::format("Frame {}", nextFrameIdx), 0x252515)
            FrameContext frameCtx {};

            LogInfo("  frame=%i\n", nextFrameIdx++);
            int nextNodeIdx = 1;

            for (auto& node : m_allNodes) {
                LogInfo("    node=%i (%s)\n", nextNodeIdx++, node->name().c_str());
                frameRegistry->setCurrentNode(node->name());
                auto executeCallback = node->constructFrame(*frameRegistry);
                frameCtx.nodeContexts.push_back({ .node = node.get(),
                                                  .executeCallback = executeCallback });
            }

            m_frameContexts[frameRegistry] = frameCtx;
        }
    }

    nodeRegistry.setCurrentNode("-");
    for (auto& frameRegistry : frameRegistries) {
        frameRegistry->setCurrentNode("-");
    }
}

void RenderPipeline::forEachNodeInResolvedOrder(const Registry& frameManager, std::function<void(std::string nodeName, AvgElapsedTimer& timer, const RenderPipelineNode::ExecuteCallback&)> callback) const
{
    auto entry = m_frameContexts.find(&frameManager);
    ASSERT(entry != m_frameContexts.end());

    // TODO: We also have to make sure that nodes rendering to the screen are last (and in some respective order that makes sense)
    // TODO: Actually run the callback in the correctly resolved order!

    const FrameContext& frameContext = entry->second;
    for (auto& [node, execCallback] : frameContext.nodeContexts) {
        callback(node->name(), node->timer(), execCallback);
    }
}

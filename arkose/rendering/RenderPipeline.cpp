#include "RenderPipeline.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include "rendering/scene/GpuScene.h"
#include <fmt/format.h>

RenderPipeline::RenderPipeline(GpuScene* scene)
    : m_scene(scene)
{
    // Add "Scene" node which should always be included (unless it's some weird case that I can't think of now)
    m_allNodes.push_back(scene);
}

void RenderPipeline::addNode(const std::string& name, RenderPipelineLambdaNode::ConstructorFunction constructorFunction)
{
    addNode(std::make_unique<RenderPipelineLambdaNode>(name, constructorFunction));
}

RenderPipelineNode& RenderPipeline::addNode(std::unique_ptr<RenderPipelineNode>&& node)
{
    // All nodes should be added before construction!
    ARKOSE_ASSERT(m_nodeContexts.empty());

    m_ownedNodes.emplace_back(std::move(node));
    m_allNodes.push_back(m_ownedNodes.back().get());

    return *m_ownedNodes.back().get();
}

void RenderPipeline::constructAll(Registry& registry)
{
    SCOPED_PROFILE_ZONE();

    // TODO: This is slightly confusing.. why not make this "destruction" more explicit?
    m_nodeContexts.clear();

    ARKOSE_LOG(Info, "Constructing node resources:");
    for (auto& node : m_allNodes) {

        SCOPED_PROFILE_ZONE_DYNAMIC(node->name(), 0x252515)
        ARKOSE_LOG(Info, " {}", node->name());

        registry.setCurrentNode({}, node->name());
        auto executeCallback = node->construct(*m_scene, registry);

        m_nodeContexts.push_back({ .node = node,
                                   .executeCallback = std::move(executeCallback) });
    }

    registry.setCurrentNode({}, std::nullopt);
}

void RenderPipeline::forEachNodeInResolvedOrder(const Registry& frameManager, std::function<void(std::string nodeName, AvgElapsedTimer& timer, const RenderPipelineNode::ExecuteCallback&)> callback) const
{
    // TODO: Actually run the callback in the correctly resolved order!
    // TODO: We also have to make sure that nodes rendering to the screen are last (and in some respective order that makes sense)

    ARKOSE_ASSERT(m_nodeContexts.size() > 0);

    for (auto& [node, execCallback] : m_nodeContexts) {
        callback(node->name(), node->timer(), execCallback);
    }
}

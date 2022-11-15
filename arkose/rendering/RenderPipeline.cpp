#include "RenderPipeline.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include "rendering/GpuScene.h"
#include <fmt/format.h>
#include <imgui.h>

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

    node->setPipeline({}, *this);

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

        SCOPED_PROFILE_ZONE_DYNAMIC(node->name(), 0x252515);
        ARKOSE_LOG(Info, " {}", node->name());

        registry.setCurrentNode({}, node->name());
        auto executeCallback = node->construct(*m_scene, registry);

        m_nodeContexts.push_back({ .node = node,
                                   .executeCallback = std::move(executeCallback) });
    }

    registry.setCurrentNode({}, std::nullopt);
}

void RenderPipeline::forEachNodeInResolvedOrder(const Registry& frameManager, std::function<void(RenderPipelineNode&, const RenderPipelineNode::ExecuteCallback&)> callback) const
{
    // TODO: Actually run the callback in the correctly resolved order!
    // TODO: We also have to make sure that nodes rendering to the screen are last (and in some respective order that makes sense)

    ARKOSE_ASSERT(m_nodeContexts.size() > 0);

    for (auto& [node, execCallback] : m_nodeContexts) {
        callback(*node, execCallback);
    }
}

void RenderPipeline::drawGui(bool includeContainingWindow) const
{
    if (includeContainingWindow) {
        ImGui::Begin("Render Pipeline");
    }

    std::string frameTimePerfString = m_pipelineTimer.createFormattedString();
    ImGui::Text("Pipline frame time: %s", frameTimePerfString.c_str());

    if (ImGui::TreeNode("Frame time plots")) {

        static float plotRangeMin = 0.0f;
        static float plotRangeMax = 16.667f;
        ImGui::SliderFloat("Plot range min", &plotRangeMin, 0.0f, plotRangeMax);
        ImGui::SliderFloat("Plot range max", &plotRangeMax, plotRangeMin, 40.0f);
        static float plotHeight = 160.0f;
        ImGui::SliderFloat("Plot height", &plotHeight, 40.0f, 350.0f);

        m_pipelineTimer.plotTimes(plotRangeMin, plotRangeMax, plotHeight);

        ImGui::TreePop();
    }

    for (auto& [node, execCallback] : m_nodeContexts) {
        std::string nodeName = node->name();
        std::string nodeTimePerfString = node->timer().createFormattedString();
        std::string nodeTitle = fmt::format("{} | {}###{}", nodeName, nodeTimePerfString, nodeName);
        if (ImGui::CollapsingHeader(nodeTitle.c_str())) {
            node->drawGui();
        }
    }

    if (includeContainingWindow) {
        ImGui::End();
    }
}

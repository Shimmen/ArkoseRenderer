#pragma once

#include "AppState.h"
#include "Registry.h"
#include "RenderPipelineNode.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class RenderPipeline {
public:

    ////////////////////////////////////////////////////////////////////////////
    // Pipeline setup

    explicit RenderPipeline(GpuScene* scene);
    ~RenderPipeline() = default;

    RenderPipeline(RenderPipeline&) = delete;
    RenderPipeline& operator=(RenderPipeline&) = delete;

    void addNode(const std::string& name, RenderPipelineLambdaNode::ConstructorFunction);
    RenderPipelineNode& addNode(std::unique_ptr<RenderPipelineNode>&&);

    template<typename NodeType, typename... Args>
    NodeType& addNode(Args&&... args)
    {
        auto nodePtr = std::make_unique<NodeType>(std::forward<Args>(args)...);
        return static_cast<NodeType&>(addNode(std::move(nodePtr)));
    }

    ////////////////////////////////////////////////////////////////////////////
    // Pipeline execution

    void constructAll(Registry& registry);

    // The callback is called for each node (in correct order)
    void forEachNodeInResolvedOrder(const Registry&, std::function<void(RenderPipelineNode&, const RenderPipelineNode::ExecuteCallback&)>) const;

    AvgElapsedTimer& timer() { return m_pipelineTimer; }
    void drawGui(bool includeContainingWindow = false) const;

    ////////////////////////////////////////////////////////////////////////////
    // Data & functions for cross-node communication

    std::vector<RenderPipelineNode*> const& nodes() const { return m_allNodes; }

    Extent2D outputResolution() const { return m_outputResolution; }
    void setOutputResolution(Extent2D outputRes) { m_outputResolution = outputRes; }

    Extent2D renderResolution() const { return m_renderResolution; }
    void setRenderResolution(Extent2D renderRes) { m_renderResolution = renderRes; }

    // TODO: Now when nodes have access to the render pipeline we can use this to store various info about the current.. pipeline!
    // Any cross-node communication can be done through this. They can explicitly put data here, e.g. a list of lights that will get
    // ray traced shadows and another for lights that will get shadow maps, or they can essentially register interfaces; a shadow
    // management interface which all the other nodes can interact with without knowing the exact nodes involved in it.

private:

    // All nodes that are part of this pipeline (some may be not not owned)
    std::vector<std::unique_ptr<RenderPipelineNode>> m_ownedNodes {};
    std::vector<RenderPipelineNode*> m_allNodes {};

    struct NodeContext {
        RenderPipelineNode* node;
        RenderPipelineNode::ExecuteCallback executeCallback;
    };

    std::vector<NodeContext> m_nodeContexts {};
    AvgElapsedTimer m_pipelineTimer {};

    Extent2D m_outputResolution {};
    Extent2D m_renderResolution {};

    GpuScene* m_scene {};
};

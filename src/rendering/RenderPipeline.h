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
    explicit RenderPipeline(GpuScene* scene);
    ~RenderPipeline() = default;

    RenderPipeline(RenderPipeline&) = delete;
    RenderPipeline& operator=(RenderPipeline&) = delete;

    void addNode(const std::string& name, RenderPipelineLambdaNode::ConstructorFunction);
    void addNode(std::unique_ptr<RenderPipelineNode>&&);

    template<typename NodeType, typename... Args>
    void addNode(Args&&... args)
    {
        auto nodePtr = std::make_unique<NodeType>(std::forward<Args>(args)...);
        addNode(std::move(nodePtr));
    }

    void constructAll(Registry& registry);

    // The callback is called for each node (in correct order)
    void forEachNodeInResolvedOrder(const Registry&, std::function<void(std::string, AvgElapsedTimer&, const RenderPipelineNode::ExecuteCallback&)>) const;

private:

    // All nodes that are part of this pipeline (some may be not not owned)
    std::vector<std::unique_ptr<RenderPipelineNode>> m_ownedNodes {};
    std::vector<RenderPipelineNode*> m_allNodes {};

    struct NodeContext {
        RenderPipelineNode* node;
        RenderPipelineNode::ExecuteCallback executeCallback;
    };

    std::vector<NodeContext> m_nodeContexts {};

    GpuScene* m_scene {};
};

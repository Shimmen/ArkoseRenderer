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
    explicit RenderPipeline(Scene* scene);
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
    struct NodeContext {
        RenderPipelineNode* node;
        RenderPipelineNode::ExecuteCallback executeCallback;
    };

    // All nodes that are part of this pipeline
    std::vector<std::unique_ptr<RenderPipelineNode>> m_allNodes {};

    std::vector<NodeContext> m_nodeContexts {};

    Scene* m_scene {};
};

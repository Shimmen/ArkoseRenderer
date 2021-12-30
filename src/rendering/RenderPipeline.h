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
    RenderPipeline() = default;
    ~RenderPipeline() = default;

    RenderPipeline(RenderPipeline&) = delete;
    RenderPipeline& operator=(RenderPipeline&) = delete;

    void addNode(const std::string& name, RenderPipelineBasicNode::ConstructorFunction);
    void addNode(std::unique_ptr<RenderPipelineNode>&&);

    template<typename NodeType, typename... Args>
    void addNode(Args&&... args)
    {
        auto nodePtr = std::make_unique<NodeType>(std::forward<Args>(args)...);
        addNode(std::move(nodePtr));
    }

    //! Construct all nodes & set up a per-frame context for each resource manager frameManagers
    void constructAll(Registry& nodeManager, std::vector<Registry*> frameManagers);

    //! The callback is called for each node (in correct order)
    void forEachNodeInResolvedOrder(const Registry&, std::function<void(std::string, NodeTimer&, const RenderPipelineNode::ExecuteCallback&)>) const;

private:
    struct NodeContext {
        RenderPipelineNode* node;
        RenderPipelineNode::ExecuteCallback executeCallback;
    };
    struct FrameContext {
        std::vector<NodeContext> nodeContexts {};
    };

    //! All nodes that are part of this pipeline
    std::vector<std::unique_ptr<RenderPipelineNode>> m_allNodes {};

    //! The frame contexts, one per frame (i.e. image in the swapchain)
    std::unordered_map<const Registry*, FrameContext> m_frameContexts {};
};

#pragma once

#include "AppState.h"
#include "Registry.h"
#include "Resources.h"
#include "rendering/CommandList.h"
#include <functional>
#include <memory>
#include <string>

class RenderGraphNode {
public:
    explicit RenderGraphNode(std::string name);
    virtual ~RenderGraphNode() = default;

    using ExecuteCallback = std::function<void(const AppState&, CommandList&)>;

    [[nodiscard]] const std::string& name() const;

    //! Optionally return a display name for use in GUI situations
    virtual std::optional<std::string> displayName() const { return {}; }

    //! This is not const since we need to write to members here that are shared for the whole node.
    virtual void constructNode(Registry&) {};

    //! This is const, since changing or writing to any members would probably break stuff
    //! since this is called n times, one for each frame at reconstruction.
    virtual ExecuteCallback constructFrame(Registry&) const { return RenderGraphNode::ExecuteCallback(); };

private:
    std::string m_name;
};

class RenderGraphBasicNode final : public RenderGraphNode {
public:
    using ConstructorFunction = std::function<ExecuteCallback(Registry&)>;
    RenderGraphBasicNode(std::string name, ConstructorFunction);

    void constructNode(Registry&) override;
    ExecuteCallback constructFrame(Registry&) const override;

private:
    ConstructorFunction m_constructorFunction;
};

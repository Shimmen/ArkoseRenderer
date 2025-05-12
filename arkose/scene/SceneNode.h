#pragma once

#include <ark/handle.h>
#include <ark/copying.h>
#include "core/Badge.h"
#include "scene/Transform.h"
#include "scene/editor/EditorObject.h"

class Scene;

ARK_DEFINE_HANDLE_TYPE(SceneNodeHandle)

class SceneNode : public IEditorObject {
public:
    SceneNode(Scene& ownerScene, Transform transform, std::string_view name);
    ~SceneNode();

    std::string_view name() const { return m_name; }

    SceneNodeHandle parent() const { return m_parent; }
    void setParent(SceneNodeHandle parent);

    std::vector<SceneNodeHandle> const& children() const { return m_children; }

    SceneNodeHandle handle() const { return m_handle; }
    void setHandle(SceneNodeHandle handle, Badge<Scene>) { m_handle = handle; }

    // IEditorObject interface
    bool shouldDrawGui() const override { return false; }

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

private:
    friend class Scene;

    Transform m_transform;
    std::string m_name;

    SceneNodeHandle m_handle {};
    SceneNodeHandle m_parent {};
    std::vector<SceneNodeHandle> m_children {};
    Scene* m_scene { nullptr };
};

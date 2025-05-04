#include "SceneNode.h"

#include "core/Types.h"
#include "scene/Scene.h"

SceneNode::SceneNode(Scene& ownerScene, Transform localTransform, std::string_view name)
    : m_scene(&ownerScene)
    , m_transform(localTransform)
{
    if (name.empty()) {
        static u64 nextNodeIdx = 0;
        m_name = std::format("Node{}", nextNodeIdx++);
    } else {
        m_name = name;
    }
}

SceneNode::~SceneNode()
{
}

void SceneNode::setParent(SceneNodeHandle parent)
{
    ARKOSE_ASSERTM(m_handle.valid(), "Ensure self-handle is valid before a parent is set (should be handled by Scene)");

    m_parent = parent;

    if (SceneNode* parentSceneNode = m_scene->node(m_parent)) {
        m_transform.setParent(&parentSceneNode->transform());
        parentSceneNode->m_children.push_back(m_handle);
    }
}

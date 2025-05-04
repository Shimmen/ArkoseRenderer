#pragma once

#include "scene/MeshInstance.h"
#include "scene/editor/EditorGizmo.h"
#include "scene/lights/Light.h"
#include <vector>

class Scene;

class EditorScene final {
public:
    EditorScene(Scene&);
    ~EditorScene();

    void update(float elapsedTime, float deltaTime);

    // Meta

    void clearSelectedObject();
    void setSelectedObject(IEditorObject&);
    void setSelectedObject(Light& light);
    void setSelectedObject(StaticMeshInstance& meshInstance);
    IEditorObject* selectedObject() { return m_selectedObject; }

    EditorGizmo* raycastScreenPointAgainstEditorGizmos(vec2 screenPoint);

    // GUI

    void drawGui();

    void drawInstanceBoundingBox(StaticMeshInstance const&);
    void drawInstanceBoundingBox(SkeletalMeshInstance const&);
    void drawInstanceSkeleton(SkeletalMeshInstance const&);
    void drawSceneGizmos();

private:
    Scene& m_scene;

    IEditorObject* m_selectedObject { nullptr };

    bool m_shouldDrawAllInstanceBoundingBoxes { false };
    bool m_shouldDrawSelectedInstanceBoundingBox { false };
    bool m_shouldDrawAllSkeletons { false };
    bool m_shouldDrawSelectedInstanceSkeleton { false };

    bool m_shouldDrawGizmos { false };
    std::vector<EditorGizmo> m_editorGizmos {};
};

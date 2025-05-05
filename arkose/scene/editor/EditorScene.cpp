#include "EditorScene.h"

#include "system/Input.h"
#include "scene/Scene.h"
#include "rendering/GpuScene.h"
#include "rendering/RenderPipeline.h"
#include "rendering/debug/DebugDrawer.h"
#include <imgui.h>
#include <ImGuizmo.h>

EditorScene::EditorScene(Scene& scene)
    : m_scene(scene)
{
}

EditorScene::~EditorScene()
{
}

void EditorScene::update(float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    if (Input::instance().wasKeyReleased(Key::Escape)) {
        clearSelectedObject();
    }

    drawSceneGizmos();
}

void EditorScene::clearSelectedObject()
{
    m_selectedObject = nullptr;
}

void EditorScene::setSelectedObject(IEditorObject& editorObject)
{
    m_selectedObject = &editorObject;
}

void EditorScene::setSelectedObject(Light& light)
{
    // TODO: Also track type?
    m_selectedObject = &light;
}
void EditorScene::setSelectedObject(StaticMeshInstance& meshInstance)
{
    // TODO: Also track type?
    m_selectedObject = &meshInstance;
}

EditorGizmo* EditorScene::raycastScreenPointAgainstEditorGizmos(vec2 screenPoint)
{
    // `screenPoint` is a point in the output resolution but internally for
    // everything about a scene we only care about the render resolution.
    vec2 renderResolution = m_scene.gpuScene().pipeline().renderResolution().asFloatVector();
    vec2 outputResolution = m_scene.gpuScene().pipeline().outputResolution().asFloatVector();
    screenPoint *= renderResolution / outputResolution;

    EditorGizmo* closestGizmo = nullptr;

    for (EditorGizmo& gizmo : m_editorGizmos) {
        if (gizmo.isScreenPointInside(screenPoint)) {
            if (closestGizmo == nullptr || gizmo.distanceFromCamera() < closestGizmo->distanceFromCamera()) {
                closestGizmo = &gizmo;
            }
        }
    }

    return closestGizmo;
}

void EditorScene::drawGui()
{
    if (ImGui::TreeNode("Visualisations")) {
        ImGui::Checkbox("Draw all mesh bounding boxes", &m_shouldDrawAllInstanceBoundingBoxes);
        ImGui::Checkbox("Draw bounding box of the selected mesh instance", &m_shouldDrawSelectedInstanceBoundingBox);
        ImGui::Separator();
        ImGui::Checkbox("Draw all mesh skeletons", &m_shouldDrawAllSkeletons);
        ImGui::Checkbox("Draw skeleton of the selected mesh instance", &m_shouldDrawSelectedInstanceSkeleton);
        ImGui::TreePop();
    }
}

void EditorScene::drawSceneNodeHierarchy()
{
    ImGui::Begin("Scene");
    drawSceneNodeHierarchyRecursive(m_scene.rootNode());
    ImGui::End();
}

void EditorScene::drawSceneNodeHierarchyRecursive(SceneNodeHandle currentNode)
{
    SceneNode* node = m_scene.node(currentNode);

    ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (node->children().empty()) {
        treeNodeFlags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (node == selectedObject()) {
        treeNodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    if (ImGui::TreeNodeEx(node->name().data(), treeNodeFlags)) {

        if (ImGui::IsItemClicked() && currentNode != m_scene.rootNode()) {
            setSelectedObject(*node);
        }

        for (SceneNodeHandle child : node->children()) {
            drawSceneNodeHierarchyRecursive(child);
        }

        ImGui::TreePop();
    }
}

void EditorScene::drawInstanceBoundingBox(StaticMeshInstance const& instance)
{
    if (StaticMesh* staticMesh = m_scene.gpuScene().staticMeshForHandle(instance.mesh())) {
        ark::aabb3 transformedAABB = staticMesh->boundingBox().transformed(instance.transform().worldMatrix());
        DebugDrawer::get().drawBox(transformedAABB.min, transformedAABB.max, Colors::white);
    }
}

void EditorScene::drawInstanceBoundingBox(SkeletalMeshInstance const& instance)
{
    if (SkeletalMesh* skeletalMesh = m_scene.gpuScene().skeletalMeshForHandle(instance.mesh())) {
        // TODO: Use an animated bounding box! The static one is only guaranteed to be bounding for the rest pose
        ark::aabb3 transformedAABB = skeletalMesh->underlyingMesh().boundingBox().transformed(instance.transform().worldMatrix());
        DebugDrawer::get().drawBox(transformedAABB.min, transformedAABB.max, Colors::white);
    }
}

void EditorScene::drawInstanceSkeleton(SkeletalMeshInstance const& instance)
{
    DebugDrawer::get().drawSkeleton(instance.skeleton(), instance.transform().worldMatrix());
}

void EditorScene::drawSceneGizmos()
{
    // Reset "persistent" gizmos
    m_editorGizmos.clear();

    static ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    static ImGuizmo::MODE mode = ImGuizmo::WORLD;

    auto& input = Input::instance();
    if (not input.isButtonDown(Button::Right) && not input.isGuiUsingKeyboard()) {
        if (input.wasKeyPressed(Key::W))
            operation = ImGuizmo::TRANSLATE;
        else if (input.wasKeyPressed(Key::E))
            operation = ImGuizmo::ROTATE;
        else if (input.wasKeyPressed(Key::R))
            operation = ImGuizmo::SCALE;
    }

    if (input.wasKeyPressed(Key::Y) && not input.isGuiUsingKeyboard()) {
        if (mode == ImGuizmo::LOCAL) {
            mode = ImGuizmo::WORLD;
        } else if (mode == ImGuizmo::WORLD) {
            mode = ImGuizmo::LOCAL;
        }
    }

    if (input.wasKeyPressed(Key::G)) {
        m_shouldDrawGizmos = not m_shouldDrawGizmos;
    }

    if (m_shouldDrawGizmos) {
        // Light gizmos
        m_scene.forEachLight([this](size_t idx, Light& light) {
            Icon const& lightbulbIcon = m_scene.gpuScene().iconManager().lightbulb();
            IconBillboard iconBillboard = lightbulbIcon.asBillboard(m_scene.camera(), light.transform().positionInWorld());
            DebugDrawer::get().drawIcon(iconBillboard, light.color());

            EditorGizmo gizmo { iconBillboard, light };
            gizmo.debugName = light.name();
            m_editorGizmos.push_back(gizmo);
        });
    }

    if (m_shouldDrawAllInstanceBoundingBoxes) {
        for (auto const& instance : m_scene.gpuScene().staticMeshInstances()) {
            drawInstanceBoundingBox(*instance);
        }
        for (auto const& instance : m_scene.gpuScene().skeletalMeshInstances()) {
            drawInstanceBoundingBox(*instance);
        }
    }
    if (m_shouldDrawAllSkeletons) {
        for (auto const& instance : m_scene.gpuScene().skeletalMeshInstances()) {
            drawInstanceSkeleton(*instance);
        }
    }

    if (selectedObject()) {

        if (m_shouldDrawSelectedInstanceBoundingBox || m_shouldDrawSelectedInstanceSkeleton) {
            if (auto* staticInstance = dynamic_cast<StaticMeshInstance*>(selectedObject())) {
                if (m_shouldDrawSelectedInstanceBoundingBox) {
                    drawInstanceBoundingBox(*staticInstance);
                }
            } else if (auto* skeletalInstance = dynamic_cast<SkeletalMeshInstance*>(selectedObject())) {
                if (m_shouldDrawSelectedInstanceBoundingBox) {
                    drawInstanceBoundingBox(*skeletalInstance);
                }
                if (m_shouldDrawSelectedInstanceSkeleton) {
                    drawInstanceSkeleton(*skeletalInstance);
                }
            }
        }

        if (selectedObject()->shouldDrawGui()) {

            constexpr float defaultWindowWidth = 480.0f;
            vec2 windowPosition = vec2(ImGui::GetIO().DisplaySize.x - defaultWindowWidth - 16.0f, 32.0f);
            ImGui::SetNextWindowPos(ImVec2(windowPosition.x, windowPosition.y), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(defaultWindowWidth, 600.0f), ImGuiCond_Appearing);

            bool open = true;
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

            if (ImGui::Begin("##SelectedObjectWindow", &open, flags)) {
                selectedObject()->drawGui();
            }
            ImGui::End();
        }

        Transform& selectedTransform = selectedObject()->transform();

        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

        mat4 viewMatrix = m_scene.camera().viewMatrix();
        mat4 projMatrix = m_scene.camera().projectionMatrix();

        // Silly stuff, since ImGuizmo doesn't seem to like my projection matrix..
        projMatrix.y = -projMatrix.y;

        mat4 matrix = selectedTransform.localMatrix();
        if (ImGuizmo::Manipulate(value_ptr(viewMatrix), value_ptr(projMatrix), operation, mode, value_ptr(matrix))) {
            selectedTransform.setFromMatrix(matrix);
        }
    }
}

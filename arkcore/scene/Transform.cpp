#include "Transform.h"

#include <imgui.h>

void Transform::drawGui()
{
    bool changed = false;

    changed |= ImGui::DragFloat3("Translation", value_ptr(m_translation), 0.01f);

    vec3 eulerAnglesDegrees = toDegrees(quatToEulerAngles(m_orientation));
    if (ImGui::DragFloat3("Orientation", value_ptr(eulerAnglesDegrees), 1.0f)) {
        m_orientation = normalize(quatFromEulerAngles(toRadians(eulerAnglesDegrees)));
        changed |= true;
    }

    changed |= ImGui::DragFloat3("Scale", value_ptr(m_scale), 0.01f);

    if (changed) {
        m_matrix = {};
        m_normalMatrix = {};
    }
}

void Transform::setParent(Transform const* parent)
{
    m_parent = parent;
    m_matrix = {};
    m_normalMatrix = {};
}

Transform Transform::flattened() const
{
    vec3 globalTranslation = positionInWorld();
    quat globalOrientation = orientationInWorld();
    vec3 globalScale = scaleInWorld();

    return Transform(globalTranslation, globalOrientation, globalScale);
}

void Transform::setPositionInWorld(vec3 worldPosition)
{
    vec3 newLocalTranslation = worldPosition;
    if (m_parent != nullptr) {
        newLocalTranslation -= m_parent->positionInWorld();
    }

    setTranslation(newLocalTranslation);
}

void Transform::setOrientationInWorld(quat worldOrientation)
{
    worldOrientation = normalize(worldOrientation);

    quat newLocalOrientation = (m_parent != nullptr)
        ? conjugate(m_parent->orientationInWorld()) * worldOrientation
        : worldOrientation;

    setOrientation(newLocalOrientation);
}

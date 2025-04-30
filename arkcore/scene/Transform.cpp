#include "Transform.h"

void Transform::setParent(Transform const* parent)
{
    m_parent = parent;
    m_matrix = {};
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

#include "Transform.h"

Transform Transform::flattened() const
{
    vec3 globalTranslation = positionInWorld();
    quat globalOrientation = orientationInWorld();
    vec3 globalScale = localScale(); // NOTE: Scale does not propagate!

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
    quat newLocalOrientation = (m_parent != nullptr)
        ? ark::inverse(m_parent->orientationInWorld()) * worldOrientation
        : worldOrientation;

    setOrientation(newLocalOrientation);
}

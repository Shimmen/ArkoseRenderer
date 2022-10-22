#include "Transform.h"

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

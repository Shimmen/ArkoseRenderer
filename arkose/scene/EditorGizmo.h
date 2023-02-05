#pragma once

class Camera;

#include "rendering/Icon.h"
#include "scene/Transform.h"
#include <ark/vector.h>
#include <string>

class EditorGizmo {
public:
    EditorGizmo(IconBillboard, ITransformable&);

    bool isScreenPointInside(vec2 screenPoint) const;
    float distanceFromCamera() const;

    IconBillboard const& icon() const;
    Camera const& alignCamera() const;

    ITransformable& transformable() { return *m_transformable; }
    ITransformable const& transformable() const { return *m_transformable; }

    std::string debugName {};

private:
    IconBillboard m_icon;
    ITransformable* m_transformable;
};

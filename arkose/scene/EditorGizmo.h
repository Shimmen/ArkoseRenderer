#pragma once

class Camera;

#include "rendering/Sprite.h"
#include "scene/Transform.h"
#include <ark/vector.h>
#include <string>

class EditorGizmo {
public:
    EditorGizmo(Sprite const&, ITransformable&);

    bool isScreenPointInside(vec2 screenPoint) const;
    float distanceFromCamera() const;

    Sprite const& sprite() const;
    Camera const& alignCamera() const;

    ITransformable& transformable() { return *m_transformable; }
    ITransformable const& transformable() const { return *m_transformable; }

    std::string debugName {};

private:
    Sprite m_sprite {};
    ITransformable* m_transformable {};
};

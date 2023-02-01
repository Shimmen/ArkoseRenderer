#include "EditorGizmo.h"

#include "core/Assert.h"
#include "scene/camera/Camera.h"

EditorGizmo::EditorGizmo(Sprite const& sprite, ITransformable& transformable)
    : m_sprite(sprite)
    , m_transformable(&transformable)
{
    ARKOSE_ASSERT(sprite.alignCamera != nullptr);
}

bool EditorGizmo::isScreenPointInside(vec2 screenPoint) const
{
    vec2 projectedMin = vec2(+std::numeric_limits<float>::max());
    vec2 projectedMax = vec2(-std::numeric_limits<float>::max());

    for (int i = 0; i < 4; ++i) {
        vec4 pt = alignCamera().viewProjectionMatrix() * vec4(sprite().points[i], 1.0f);
        vec2 ptt = vec2(pt.x / pt.w, pt.y / pt.w);
        projectedMin = ark::min(projectedMin, ptt);
        projectedMax = ark::max(projectedMax, ptt);
    }

    vec2 viewport = vec2(alignCamera().viewport().width(), alignCamera().viewport().height());
    vec2 adjustedScreenPoint = (screenPoint / viewport) * vec2(2.0f) - vec2(1.0f);

    return ark::all(ark::greaterThanEqual(adjustedScreenPoint, projectedMin))
        && ark::all(ark::lessThanEqual(adjustedScreenPoint, projectedMax));
}

float EditorGizmo::distanceFromCamera() const
{
    // TODO: Should be kind of implicit.. a billboard is constructed from a single point.
    return distance(alignCamera().position(), sprite().points[0]);
}

Sprite const& EditorGizmo::sprite() const
{
    return m_sprite;
}

Camera const& EditorGizmo::alignCamera() const
{
    ARKOSE_ASSERT(m_sprite.alignCamera != nullptr);
    return *m_sprite.alignCamera;
}

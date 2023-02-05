#include "EditorGizmo.h"

#include "core/Assert.h"
#include "scene/camera/Camera.h"

EditorGizmo::EditorGizmo(IconBillboard icon, ITransformable& transformable)
    : m_icon(std::move(icon))
    , m_transformable(&transformable)
{
}

bool EditorGizmo::isScreenPointInside(vec2 screenPoint) const
{
    vec2 projectedMin = vec2(+std::numeric_limits<float>::max());
    vec2 projectedMax = vec2(-std::numeric_limits<float>::max());

    for (int i = 0; i < 4; ++i) {
        vec4 pt = alignCamera().viewProjectionMatrix() * vec4(icon().positions()[i], 1.0f);
        vec2 ptt = vec2(pt.x / pt.w, pt.y / pt.w);
        projectedMin = ark::min(projectedMin, ptt);
        projectedMax = ark::max(projectedMax, ptt);
    }

    vec2 viewport = vec2(alignCamera().viewport().width(), alignCamera().viewport().height());
    vec2 ndcScreenPoint = (screenPoint / viewport) * vec2(2.0f) - vec2(1.0f);

    if (ark::all(ark::greaterThanEqual(ndcScreenPoint, projectedMin))
        && ark::all(ark::lessThanEqual(ndcScreenPoint, projectedMax))) {

        if (ImageAsset const* image = icon().icon().image()) {
            vec2 imageUv = ark::inverseLerp(ndcScreenPoint, projectedMin, projectedMax);
            vec2 imagePixel = imageUv * vec2(image->width() - 1, image->height() - 1);
            u32 x = static_cast<u32>(std::round(imagePixel.x));
            u32 y = static_cast<u32>(std::round(imagePixel.y));

            ImageAsset::rgba8 pixel = image->getPixelAsRGBA8(x, y, 0, 0);
            if (pixel.w < 0.2f) {
                return false;
            }
        }

        return true;
    }

    return false;
}

float EditorGizmo::distanceFromCamera() const
{
    // TODO: Should be kind of implicit.. a billboard is constructed from a single point.
    return distance(alignCamera().position(), icon().positions()[0]);
}

IconBillboard const& EditorGizmo::icon() const
{
    return m_icon;
}

Camera const& EditorGizmo::alignCamera() const
{
    return m_icon.camera();
}

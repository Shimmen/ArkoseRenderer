#include "Icon.h"

#include "scene/camera/Camera.h"

IconBillboard Icon::asBillboard(Camera const& camera, vec3 position, vec2 size) const
{
    return IconBillboard::createFromIcon(*this, camera, position, size);
}

IconBillboard::IconBillboard(Icon const& icon, Camera const& camera, std::array<vec3, 4> positions, std::array<vec2, 4> texCoords)
    : m_icon(&icon)
    , m_camera(&camera)
    , m_positions(std::move(positions))
    , m_texCoords(std::move(texCoords))
{
}

IconBillboard IconBillboard::createFromIcon(Icon const& icon, Camera const& camera, vec3 position, vec2 minSize, float scaleDistance)
{
    vec2 halfSize = minSize / 2.0f;
    halfSize *= std::max(1.0f, distance(position, camera.position()) / scaleDistance);

    std::array<vec3, 4> positions { position - halfSize.x * camera.right() - halfSize.y * camera.up(),
                                    position - halfSize.x * camera.right() + halfSize.y * camera.up(),
                                    position + halfSize.x * camera.right() + halfSize.y * camera.up(),
                                    position + halfSize.x * camera.right() - halfSize.y * camera.up() };

    // NOTE: We use the world coordinate system of origin in the bottom left corner
    std::array<vec2, 4> texCoords { vec2(0.0f, 1.0f),
                                    vec2(0.0f, 0.0f),
                                    vec2(1.0f, 0.0f),
                                    vec2(1.0f, 1.0f) };

    return IconBillboard(icon, camera, std::move(positions), std::move(texCoords));
}

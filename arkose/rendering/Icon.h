#pragma once

class Camera;
class ImageAsset;
class Texture;

#include <ark/copying.h>
#include "rendering/backend/base/Texture.h"
#include <ark/vector.h>
#include <array>
#include <memory>

class IconBillboard;

class Icon {
public:
    Icon() = default;
    Icon(ImageAsset* image, std::unique_ptr<Texture> texture)
        : m_image(image)
        , m_texture(std::move(texture))
    {
    }

    ImageAsset const* image() const { return m_image; }
    Texture const* texture() const { return m_texture.get(); }

    IconBillboard asBillboard(Camera const&, vec3 position, vec2 size = vec2(0.25f)) const;

private:
    ImageAsset* m_image { nullptr };
    std::unique_ptr<Texture> m_texture { nullptr };
};

class IconBillboard {
public:
    static IconBillboard createFromIcon(Icon const&, Camera const&, vec3 position, vec2 minSize = vec2(0.25f), float scaleDistance = 5.0f);

    Icon const& icon() const { return *m_icon; }
    Camera const& camera() const { return *m_camera; }

    std::array<vec3, 4> const& positions() const { return m_positions; }
    std::array<vec2, 4> const& texCoords() const { return m_texCoords; }

private:
    IconBillboard(Icon const&, Camera const&, std::array<vec3, 4> positions, std::array<vec2, 4> texCoords);

    Icon const* m_icon { nullptr };
    Camera const* m_camera { nullptr };

    //
    // The 4 points are defined as follows: 
    //
    // 1--2
    // | /|
    // |/ |
    // 0--3
    //
    // Hence, if drawing as counter-clockwise triangles,
    // draw points in order: 0, 2, 1; 0, 3, 2.
    //
    std::array<vec3, 4> m_positions;
    std::array<vec2, 4> m_texCoords;
};

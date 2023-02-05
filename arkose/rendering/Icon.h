#pragma once

class ImageAsset;
class Texture;

#include "core/NonCopyable.h"
#include <memory>

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

private:
    ImageAsset* m_image { nullptr };
    std::unique_ptr<Texture> m_texture { nullptr };
};

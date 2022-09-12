#pragma once

class Image;

#include <memory>

class TextureCompressor {
public:

    TextureCompressor() = default;
    ~TextureCompressor() = default;

    // For most 8-bit RGB(A) textures
    std::unique_ptr<Image> compressBC7(Image const&);

};

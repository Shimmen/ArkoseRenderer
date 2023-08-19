#pragma once

class ImageAsset;

#include <memory>

class TextureCompressor {
public:

    TextureCompressor() = default;
    ~TextureCompressor() = default;

    // For most 8-bit RGB(A) textures
    std::unique_ptr<ImageAsset> compressBC7(ImageAsset const&);

    // For normal maps, where the B-component is discarded
    std::unique_ptr<ImageAsset> compressBC5(ImageAsset const&);

};

#pragma once

#include "asset/AssetHelpers.h"
#include <string>
#include <string_view>

// Generated flatbuffer code
#include "ImageAsset_generated.h"

class StreamedImageAsset;

using ImageFormat = Arkose::Asset::ImageFormat;
using ColorSpace = Arkose::Asset::ColorSpace;

class ImageAsset: public Arkose::Asset::ImageAssetT {
public:
    ImageAsset();
    ~ImageAsset();

    // Create a new ImageAsset that is a copy of the passed in image asset but with replaced image format. The data of the new format is passed in at constuction time.
    static std::unique_ptr<ImageAsset> createCopyWithReplacedFormat(ImageAsset const&, ImageFormat, uint8_t const* data, size_t size);

    // Create a new ImageAsset from an image on disk, e.g. png or jpg. This can then be modified in place and finally be written to disk (as an .argimg)
    static std::unique_ptr<ImageAsset> createFromSourceAsset(std::string const& sourceAssetFilePath);
    static std::unique_ptr<ImageAsset> createFromSourceAsset(uint8_t const* data, size_t size);

    // Load an image asset (cached) from an .arkimg file
    // TODO: Figure out how we want to return this! Basic type, e.g. ImageAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static ImageAsset* loadFromArkimg(std::string const& filePath);

    // Load an image asset (cached) from an .arkimg file or create from source asset, depending on the file extension
    // TODO: Figure out how we want to return this! Basic type, e.g. ImageAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static ImageAsset* loadOrCreate(std::string const& filePath);

    // Load in the specified asset with minimal copying for optimal performance. Remember to unload() the streamed asset when done with it.
    static StreamedImageAsset loadForStreaming(std::string const& filePath);

    bool writeToArkimg(std::string_view filePath);

    // Apply lossless compression on the pixel data
    bool compress(int compressionLevel = 10);
    bool decompress();

    std::string_view assetFilePath() const { return m_assetFilePath; }

private:
    // Construct an image asset from a loaded flatbuffer image asset file
    ImageAsset(Arkose::Asset::ImageAsset const*, std::string filePath);

    std::string m_assetFilePath {};
};

class StreamedImageAsset {
public:
    StreamedImageAsset() = default;
    StreamedImageAsset(uint8_t* memory, Arkose::Asset::ImageAsset const* asset);

    StreamedImageAsset(StreamedImageAsset&) = delete;
    StreamedImageAsset(StreamedImageAsset&&) noexcept;

    ~StreamedImageAsset() { unload(); }
    void unload();

    Arkose::Asset::ImageAsset const* asset() const { return m_asset; }
    bool isNull() const { return m_memory == nullptr || m_asset == nullptr; }

private:
    uint8_t* m_memory { nullptr };
    Arkose::Asset::ImageAsset const* m_asset { nullptr };
};

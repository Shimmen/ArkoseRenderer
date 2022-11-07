#pragma once

#include "asset/AssetHelpers.h"
#include "core/Types.h"
#include <span>
#include <string>
#include <string_view>


enum class ColorSpace {
    Data = 0,
    sRGB_linear,
    sRGB_encoded,
};

enum class ImageFormat {
    Unknown = 0,

    // 8-bit per component formats
    R8 = 100,
    RG8,
    RGB8,
    RGBA8,

    // 32-bit float formats
    R32F = 200,
    RG32F,
    RGB32F,
    RGBA32F,

    // Block-compressed formats
    BC7 = 300,
};

class ImageAsset {
public:
    ImageAsset();
    ~ImageAsset();

    static constexpr const char* AssetFileExtension = "arkimg";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'i', 'm', 'g' };

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

    bool writeToArkimg(std::string_view filePath);

    template<class Archive>
    void serialize(Archive& ar);

    u32 width() const { return m_width; }
    u32 height() const { return m_height; }
    u32 depth() const { return m_depth; }

    ImageFormat format() const { return m_format; }

    ColorSpace colorSpace() const { return m_colorSpace; }
    void setColorSpace(ColorSpace colorSpace) { m_colorSpace = colorSpace; }

    size_t numMips() const { return m_mips.size(); }
    std::span<u8 const> pixelDataForMip(size_t mip) const;

    // Apply lossless compression on the pixel data
    bool compress(int compressionLevel = 10);
    bool decompress();

    bool isCompressed() const { return m_compressed; }
    bool isUncompressed() const { return not m_compressed; }

    std::string_view assetFilePath() const { return m_assetFilePath; }

    bool hasSourceAsset() const { return not m_sourceAssetFilePath.empty(); }
    std::string_view sourceAssetFilePath() const { return m_sourceAssetFilePath; }

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

private:
    u32 m_width { 1 };
    u32 m_height { 1 };
    u32 m_depth { 1 };

    ImageFormat m_format { ImageFormat::RGBA8 };
    ColorSpace m_colorSpace { ColorSpace::sRGB_encoded };

    // Pixel data binary blob
    std::vector<u8> m_pixelData {};

    struct ImageMip {
        size_t offset;
        size_t size;
    };
    std::vector<ImageMip> m_mips {};

    // Optional lossless compression applied to `pixelData`
    bool m_compressed { false };
    u32 m_compressedSize { 0 };
    u32 m_uncompressedSize { 0 };

    std::string m_sourceAssetFilePath {};
    std::string m_assetFilePath {}; // (this file)
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>

template<class Archive>
void serialize(Archive& archive, ImageAsset::ImageMip& mip)
{
    archive(cereal::make_nvp("offset", mip.offset),
            cereal::make_nvp("size", mip.size));
}

template<class Archive>
void ImageAsset::serialize(Archive& archive)
{
    archive(CEREAL_NVP(m_width), CEREAL_NVP(m_height), CEREAL_NVP(m_depth));
    archive(CEREAL_NVP(m_format), CEREAL_NVP(m_colorSpace));
    archive(CEREAL_NVP(m_pixelData), CEREAL_NVP(m_mips));
    archive(CEREAL_NVP(m_compressed), CEREAL_NVP(m_compressedSize), CEREAL_NVP(m_uncompressedSize));
    archive(CEREAL_NVP(m_sourceAssetFilePath));
}

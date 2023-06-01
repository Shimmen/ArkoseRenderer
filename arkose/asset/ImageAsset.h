#pragma once

#include "asset/Asset.h"
#include "core/Types.h"
#include "utility/Extent.h"
#include <ark/vector.h>
#include <span>
#include <string>
#include <string_view>


enum class ImageType {
    Unknown = 0,
    sRGBColor,
    NormalMap,
    GenericData,
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
    BC5 = 300,
    BC7 = 301,
};

struct ImageMip {
    size_t offset;
    size_t size;
};

class ImageAsset final : public Asset<ImageAsset> {
public:
    ImageAsset();
    ~ImageAsset();

    static constexpr const char* AssetFileExtension = "arkimg";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'i', 'm', 'g' };

    // Create a new ImageAsset that is a copy of the passed in image asset but with replaced image format. The data of the new format is passed in at constuction time.
    static std::unique_ptr<ImageAsset> createCopyWithReplacedFormat(ImageAsset const&, ImageFormat, std::vector<u8>&& pixelData, std::vector<ImageMip>);

    // Create a new ImageAsset from an image on disk, e.g. png or jpg. This can then be modified in place and finally be written to disk (as an .argimg)
    static std::unique_ptr<ImageAsset> createFromSourceAsset(std::string const& sourceAssetFilePath);
    static std::unique_ptr<ImageAsset> createFromSourceAsset(uint8_t const* data, size_t size);

    // Load an image asset (cached) from an .arkimg file
    // TODO: Figure out how we want to return this! Basic type, e.g. ImageAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static ImageAsset* load(std::string const& filePath);

    // Load an image asset (cached) from an .arkimg file or create from source asset, depending on the file extension
    // TODO: Figure out how we want to return this! Basic type, e.g. ImageAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static ImageAsset* loadOrCreate(std::string const& filePath);

    virtual bool readFromFile(std::string_view filePath) override;
    virtual bool writeToFile(std::string_view filePath, AssetStorage assetStorage) override;

    template<class Archive>
    void serialize(Archive& ar);

    u32 width() const { return m_width; }
    u32 height() const { return m_height; }
    u32 depth() const { return m_depth; }

    Extent3D extentAtMip(size_t mipIdx) const;

    ImageFormat format() const { return m_format; }
    bool hasCompressedFormat() const;

    ImageType type() const { return m_type; }
    void setType(ImageType type) { m_type = type; }

    size_t numMips() const { return m_mips.size(); }
    std::span<u8 const> pixelDataForMip(size_t mip) const;

    size_t totalImageSizeIncludingMips() const;

    // Generate mipmaps (slow)
    bool generateMipmaps();

    // Apply lossless compression on the pixel data
    bool compress(int compressionLevel = 10);
    bool decompress();

    bool isCompressed() const { return m_compressed; }
    bool isUncompressed() const { return not m_compressed; }

    bool hasSourceAsset() const { return not m_sourceAssetFilePath.empty(); }
    std::string_view sourceAssetFilePath() const { return m_sourceAssetFilePath; }

    using rgba8 = ark::tvec4<u8>;
    rgba8 getPixelAsRGBA8(u32 x, u32 y, u32 z, u32 mipIdx) const;

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

private:
    u32 m_width { 1 };
    u32 m_height { 1 };
    u32 m_depth { 1 };

    ImageFormat m_format { ImageFormat::RGBA8 };
    ImageType m_type { ImageType::Unknown };

    // Pixel data binary blob
    std::vector<u8> m_pixelData {};

    std::vector<ImageMip> m_mips {};

    std::vector<rgba8> pixelDataAsRGBA8(size_t mipIdx) const;

    // Optional lossless compression applied to `pixelData`
    bool m_compressed { false };
    u32 m_compressedSize { 0 };
    u32 m_uncompressedSize { 0 };

    std::string m_sourceAssetFilePath {};
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>

template<class Archive>
void serialize(Archive& archive, ImageMip& mip)
{
    archive(cereal::make_nvp("offset", mip.offset),
            cereal::make_nvp("size", mip.size));
}

template<class Archive>
void ImageAsset::serialize(Archive& archive)
{
    archive(CEREAL_NVP(m_width), CEREAL_NVP(m_height), CEREAL_NVP(m_depth));
    archive(CEREAL_NVP(m_format), CEREAL_NVP(m_type));
    archive(CEREAL_NVP(m_pixelData), CEREAL_NVP(m_mips));
    archive(CEREAL_NVP(m_compressed), CEREAL_NVP(m_compressedSize), CEREAL_NVP(m_uncompressedSize));
    archive(CEREAL_NVP(m_sourceAssetFilePath));
}

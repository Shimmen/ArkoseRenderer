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

bool imageFormatIsBlockCompressed(ImageFormat);
u32 imageFormatBlockSize(ImageFormat);

struct ImageMip {
    size_t offset;
    size_t size;
};

class ImageAsset final : public Asset<ImageAsset> {
public:
    ImageAsset();
    ~ImageAsset();

    static constexpr const char* AssetFileExtension = ".dds";

    // Create a new ImageAsset that is a copy of the passed in image asset but with replaced image format. The data of the new format is passed in at constuction time.
    static std::unique_ptr<ImageAsset> createCopyWithReplacedFormat(ImageAsset const&, ImageFormat, std::vector<u8>&& pixelData, std::vector<ImageMip>);

    // Create a new ImageAsset from an image on disk, e.g. png or jpg. This can then be modified in place and finally be written to disk (as an .argimg)
    static std::unique_ptr<ImageAsset> createFromSourceAsset(std::filesystem::path const& sourceAssetFilePath);
    static std::unique_ptr<ImageAsset> createFromSourceAsset(uint8_t const* data, size_t size);

    // Create a new ImageAsset from raw bitmap image data, i.e. rows of ImageFormat pixels according to the supplied dimensions
    static std::unique_ptr<ImageAsset> createFromRawData(uint8_t const* data, size_t size, ImageFormat, Extent2D);

    // Load an image asset (cached) from an .arkimg file
    // TODO: Figure out how we want to return this! Basic type, e.g. ImageAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static ImageAsset* load(std::filesystem::path const& filePath);

    static ImageAsset* manage(std::unique_ptr<ImageAsset>&&);

    // Load an image asset (cached) from an .arkimg file or create from source asset, depending on the file extension
    // TODO: Figure out how we want to return this! Basic type, e.g. ImageAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static ImageAsset* loadOrCreate(std::filesystem::path const& filePath);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    Extent3D extent() const { return m_extent; }
    u32 width() const { return m_extent.width(); }
    u32 height() const { return m_extent.height(); }
    u32 depth() const { return m_extent.depth(); }

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

    bool hasSourceAsset() const { return not m_sourceAssetFilePath.empty(); }
    std::filesystem::path sourceAssetFilePath() const { return std::filesystem::path(m_sourceAssetFilePath); }

    using rgba8 = ark::tvec4<u8>;
    rgba8 getPixelAsRGBA8(u32 x, u32 y, u32 z, u32 mipIdx) const;

private:
    Extent3D m_extent { 1, 1, 1 };

    ImageFormat m_format { ImageFormat::RGBA8 };
    ImageType m_type { ImageType::Unknown };

    // Pixel data binary blob
    std::vector<u8> m_pixelData {};

    std::vector<ImageMip> m_mips {};

    std::vector<rgba8> pixelDataAsRGBA8(size_t mipIdx) const;

    std::string m_sourceAssetFilePath {};
};

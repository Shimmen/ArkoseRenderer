#pragma once

#include "core/Assert.h"
#include "utility/Hash.h"
#include <string>
#include <vector>

class Image {
public:
    enum class PixelType : int {
        Grayscale = 1,
        RG = 2,
        RGB = 3,
        RGBA = 4,
    };

    enum class ComponentType {
        UInt8 = 1,
        Float = 4,
    };

    enum class CompressionType {
        Uncompressed,
        BC7,
    };

    struct Info {
        int width;
        int height;

        PixelType pixelType;
        ComponentType componentType;
        CompressionType compressionType;

        bool isHdr() const { return componentType == ComponentType::Float; }
        
        bool operator==(const Info& other) const
        {
            return width == other.width
                && height == other.height
                && pixelType == other.pixelType
                && componentType == other.componentType
                && compressionType == other.compressionType;
        }

        size_t requiredStorageSize() const
        {
            size_t componentCount = static_cast<size_t>(pixelType);
            int componentSize = static_cast<size_t>(componentType);
            size_t uncompressedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * componentCount * componentSize;

            switch (compressionType) {
            case CompressionType::Uncompressed:
                return uncompressedSize;
            case CompressionType::BC7:
                ARKOSE_ASSERT(uncompressedSize % 4 == 0);
                return uncompressedSize / 4;
            default:
                ASSERT_NOT_REACHED();
            }
        }
    };

    static Info* getInfo(const std::string& imagePath, bool quiet = false);
    static Image* load(const std::string& imagePath, PixelType, bool skipReadableCheck = false);

    const Info& info() const { return m_info; }

    const uint8_t* data() const { return m_data.data(); }
    size_t dataSize() const { return m_data.size(); }

    Image(Info, std::vector<uint8_t> data);
    ~Image() = default;

    bool operator==(const Image& other) const
    {
        return m_info == other.m_info
            && m_data == other.m_data;
    }

private:
    Info m_info;
    std::vector<uint8_t> m_data;
};

namespace std {

template<>
struct hash<Image::Info> {
    std::size_t operator()(const Image::Info& info) const
    {
        auto sizeHash = hashCombine(std::hash<int>()(info.width), std::hash<int>()(info.height));
        auto typeHash = hashCombine(std::hash<Image::PixelType>()(info.pixelType), std::hash<Image::ComponentType>()(info.componentType));
        auto compressionHash = std::hash<Image::CompressionType>()(info.compressionType);
        return hashCombine(hashCombine(sizeHash, typeHash), compressionHash);
    }
};

template<>
struct hash<Image> {
    std::size_t operator()(const Image& image) const
    {
        auto infoHash = std::hash<Image::Info>()(image.info());
        auto dataHash = std::hash<const void*>()((image.data()));
        auto sizeHash = std::hash<size_t>()((image.dataSize()));
        return hashCombine(hashCombine(infoHash, dataHash), sizeHash);
    }
};

}

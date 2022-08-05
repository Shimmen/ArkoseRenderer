#pragma once

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

    struct Info {
        int width;
        int height;

        PixelType pixelType;
        ComponentType componentType;

        bool isHdr() const { return componentType == ComponentType::Float; }
        
        bool operator==(const Info& other) const
        {
            return width == other.width
                && height == other.height
                && pixelType == other.pixelType
                && componentType == other.componentType;
        }

        size_t requiredStorageSize() const
        {
            int componentCount = static_cast<int>(pixelType);
            int componentSize = static_cast<int>(componentType);
            return width * height * componentCount * componentSize;
        }
    };

    enum class MemoryType {
        RawBitMap,
        EncodedImage,
    };

    static Info* getInfo(const std::string& imagePath, bool quiet = false);
    static Image* load(const std::string& imagePath, PixelType, bool skipReadableCheck = false);

    const Info& info() const { return m_info; }

    MemoryType memoryType() const { return m_type; }
    const void* data() const { return m_data.data(); }
    size_t size() const { return m_data.size(); }

    Image(MemoryType, Info, std::vector<uint8_t> data);
    ~Image() = default;

    bool operator==(const Image& other) const
    {
        return m_info == other.m_info
            && m_data == other.m_data
            && m_type == other.m_type;
    }

private:
    Info m_info;
    std::vector<uint8_t> m_data;
    MemoryType m_type;
};

namespace std {

template<>
struct hash<Image::Info> {
    std::size_t operator()(const Image::Info& info) const
    {
        auto sizeHash = hashCombine(std::hash<int>()(info.width), std::hash<int>()(info.height));
        auto typeHash = hashCombine(std::hash<Image::PixelType>()(info.pixelType), std::hash<Image::ComponentType>()(info.componentType));
        return hashCombine(sizeHash, typeHash);
    }
};

template<>
struct hash<Image> {
    std::size_t operator()(const Image& image) const
    {
        auto infoHash = std::hash<Image::Info>()(image.info());
        auto dataHash = std::hash<const void*>()((image.data()));
        auto sizeHash = std::hash<size_t>()((image.size()));
        auto typeHash = std::hash<Image::MemoryType>()((image.memoryType()));
        return hashCombine(hashCombine(infoHash, dataHash), hashCombine(sizeHash, typeHash));
    }
};

}

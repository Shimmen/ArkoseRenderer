#pragma once

#include <string>

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
    const void* data() const { return m_data; }
    size_t size() const { return m_size; }

    Image(Image&) = delete;
    Image& operator=(Image&) = delete;

    Image(MemoryType, Info, void* data, size_t size);
    ~Image() = default;

private:
    Info m_info;
    void* m_data; // NOTE: this memory is assumed to be owned by someone else
    size_t m_size;
    MemoryType m_type;
};

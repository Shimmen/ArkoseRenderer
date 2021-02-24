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
        UInt8,
        Float,
    };

    struct Info {
        int width;
        int height;

        PixelType pixelType;
        ComponentType componentType;

        bool isHdr() const { return componentType == ComponentType::Float; }
    };

    enum class DataOwner {
        External,
        StbImage,
    };

    static Info* getInfo(const std::string& imagePath);
    static Image* load(const std::string& imagePath, PixelType);

    const Info& info() const { return m_info; }

    DataOwner dataOwner() const { return m_owner; }
    const void* data() const { return m_data; }
    size_t size() const { return m_size; }

    Image(Image&) = delete;
    Image& operator=(Image&) = delete;

    Image(DataOwner, Info, void* data, size_t size);
    ~Image();

private:
    Info m_info;
    void* m_data;
    size_t m_size;
    DataOwner m_owner;
};

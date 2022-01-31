#pragma once

#include "backend/util/Common.h"
#include "backend/Resource.h"
#include "utility/Extent.h"
#include "utility/Image.h"
#include <memory>

class Texture : public Resource {
public:

    // (required for now, so we can create the mockup Texture objects)
    friend class VulkanBackend;

    enum class Type {
        Texture2D,
        Cubemap,
    };

    enum class Format {
        Unknown,
        R32,
        R16F,
        R32F,
        RG16F,
        RG32F,
        RGBA8,
        sRGBA8,
        RGBA16F,
        RGBA32F,
        Depth32F,
        Depth24Stencil8,
    };

    enum class MinFilter {
        Linear,
        Nearest,
    };

    enum class MagFilter {
        Linear,
        Nearest,
    };

    struct Filters {
        MinFilter min;
        MagFilter mag;

        Filters() = delete;
        Filters(MinFilter min, MagFilter mag)
            : min(min)
            , mag(mag)
        {
        }

        static Filters linear()
        {
            return {
                MinFilter::Linear,
                MagFilter::Linear
            };
        }

        static Filters nearest()
        {
            return {
                MinFilter::Nearest,
                MagFilter::Nearest
            };
        }
    };

    enum class WrapMode {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
    };

    struct WrapModes {
        WrapMode u;
        WrapMode v;
        WrapMode w;

        WrapModes() = delete;
        WrapModes(WrapMode u, WrapMode v)
            : u(u)
            , v(v)
            , w(WrapMode::ClampToEdge)
        {
        }
        WrapModes(WrapMode u, WrapMode v, WrapMode w)
            : u(u)
            , v(v)
            , w(w)
        {
        }

        static WrapModes repeatAll()
        {
            return {
                WrapMode::Repeat,
                WrapMode::Repeat,
                WrapMode::Repeat
            };
        }

        static WrapModes mirroredRepeatAll()
        {
            return {
                WrapMode::MirroredRepeat,
                WrapMode::MirroredRepeat,
                WrapMode::MirroredRepeat
            };
        }

        static WrapModes clampAllToEdge()
        {
            return {
                WrapMode::ClampToEdge,
                WrapMode::ClampToEdge,
                WrapMode::ClampToEdge
            };
        }
    };

    enum class Mipmap {
        None,
        Nearest,
        Linear,
    };

    enum class Multisampling : uint32_t {
        None = 1,
        X2 = 2,
        X4 = 4,
        X8 = 8,
        X16 = 16,
        X32 = 32,
    };

    struct TextureDescription {
        Type type;
        uint32_t arrayCount;

        Extent3D extent;
        Format format;

        MinFilter minFilter;
        MagFilter magFilter;

        WrapModes wrapMode;

        Mipmap mipmap;
        Multisampling multisampling;
    };

    Texture() = default;
    Texture(Backend&, TextureDescription);

    static void pixelFormatAndTypeForImageInfo(const Image::Info& info, bool sRGB, Texture::Format& format, Image::PixelType& pixelTypeToUse);

    static std::unique_ptr<Texture> createFromImage(Backend&, const Image&, bool sRGB, bool generateMipmaps, Texture::WrapModes);
    static std::unique_ptr<Texture> createFromPixel(Backend&, vec4 pixelColor, bool sRGB);

    static std::unique_ptr<Texture> createFromImagePath(Backend&, const std::string& imagePath, bool sRGB, bool generateMipmaps, Texture::WrapModes);
    static std::unique_ptr<Texture> createFromImagePathSequence(Backend&, const std::string& imagePathSequencePattern, bool sRGB, bool generateMipmaps, Texture::WrapModes);

    bool hasFloatingPointDataFormat() const;

    virtual void clear(ClearColor) = 0;

    virtual void setPixelData(vec4 pixel) = 0;
    virtual void setData(const void* data, size_t size) = 0;

    virtual void generateMipmaps() = 0;

    [[nodiscard]] Type type() const { return m_type; }

    [[nodiscard]] bool isArray() const { return m_arrayCount > 1; };
    [[nodiscard]] uint32_t arrayCount() const { return m_arrayCount; };

    [[nodiscard]] const Extent2D extent() const { return { m_extent.width(), m_extent.height() }; }
    [[nodiscard]] const Extent3D extent3D() const { return m_extent; }

    [[nodiscard]] Format format() const { return m_format; }

    [[nodiscard]] MinFilter minFilter() const { return m_minFilter; }
    [[nodiscard]] MagFilter magFilter() const { return m_magFilter; }
    [[nodiscard]] Filters filters() const { return { minFilter(), magFilter() }; }
    [[nodiscard]] WrapModes wrapMode() const { return m_wrapMode; }

    [[nodiscard]] Mipmap mipmap() const { return m_mipmap; }
    [[nodiscard]] bool hasMipmaps() const;
    [[nodiscard]] uint32_t mipLevels() const;

    [[nodiscard]] bool isMultisampled() const;
    [[nodiscard]] Multisampling multisampling() const;

    [[nodiscard]] bool hasDepthFormat() const
    {
        return m_format == Format::Depth32F || m_format == Format::Depth24Stencil8;
    }

    [[nodiscard]] bool hasStencilFormat() const
    {
        return m_format == Format::Depth24Stencil8;
    }

    [[nodiscard]] bool hasSrgbFormat() const
    {
        return m_format == Format::sRGBA8;
    }

private:
    Type m_type { Type::Texture2D };
    uint32_t m_arrayCount { 1u };

    Extent3D m_extent { 0, 0, 0 };
    Format m_format { Format::RGBA8 };

    MinFilter m_minFilter { MinFilter::Nearest };
    MagFilter m_magFilter { MagFilter::Nearest };

    WrapModes m_wrapMode { WrapModes::repeatAll() };

    Mipmap m_mipmap { Mipmap::None };
    Multisampling m_multisampling { Multisampling::None };
};

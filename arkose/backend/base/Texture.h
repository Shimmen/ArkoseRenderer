#pragma once

#include "backend/util/Common.h"
#include "backend/Resource.h"
#include "utility/Extent.h"
#include "utility/Hash.h"
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

        bool operator==(const Filters& rhs) const
        {
            return min == rhs.min
                && mag == rhs.mag;
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
        constexpr WrapModes(WrapMode u, WrapMode v)
            : u(u)
            , v(v)
            , w(WrapMode::ClampToEdge)
        {
        }
        constexpr WrapModes(WrapMode u, WrapMode v, WrapMode w)
            : u(u)
            , v(v)
            , w(w)
        {
        }

        static constexpr WrapModes repeatAll()
        {
            return {
                WrapMode::Repeat,
                WrapMode::Repeat,
                WrapMode::Repeat
            };
        }

        static constexpr WrapModes mirroredRepeatAll()
        {
            return {
                WrapMode::MirroredRepeat,
                WrapMode::MirroredRepeat,
                WrapMode::MirroredRepeat
            };
        }

        static constexpr WrapModes clampAllToEdge()
        {
            return {
                WrapMode::ClampToEdge,
                WrapMode::ClampToEdge,
                WrapMode::ClampToEdge
            };
        }

        bool operator==(const WrapModes& rhs) const
        {
            return u == rhs.u
                && v == rhs.v
                && w == rhs.w;
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

    struct Description {
        Type type { Type::Texture2D };
        uint32_t arrayCount { 1 };

        Extent3D extent { 1, 1, 1 };
        Format format { Format::RGBA8 };

        Filters filter { Filters::nearest() };
        WrapModes wrapMode { WrapModes::clampAllToEdge() };

        Mipmap mipmap { Mipmap::None };
        Multisampling multisampling { Multisampling::None };

        bool operator==(const Texture::Description& rhs) const
        {
            return type == rhs.type
                && arrayCount == rhs.arrayCount
                && extent == rhs.extent
                && format == rhs.format
                && filter == rhs.filter
                && wrapMode == rhs.wrapMode
                && mipmap == rhs.mipmap
                && multisampling == rhs.multisampling;
        }
    };

    Texture() = default;
    Texture(Backend&, Description);

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

    [[nodiscard]] Type type() const { return m_description.type; }

    [[nodiscard]] bool isArray() const { return m_description.arrayCount > 1; };
    [[nodiscard]] uint32_t arrayCount() const { return m_description.arrayCount; };

    [[nodiscard]] const Extent2D extent() const { return { m_description.extent.width(), m_description.extent.height() }; }
    [[nodiscard]] const Extent3D extent3D() const { return m_description.extent; }

    [[nodiscard]] Format format() const { return m_description.format; }

    [[nodiscard]] MinFilter minFilter() const { return m_description.filter.min; }
    [[nodiscard]] MagFilter magFilter() const { return m_description.filter.mag; }
    [[nodiscard]] Filters filters() const { return m_description.filter; }
    [[nodiscard]] WrapModes wrapMode() const { return m_description.wrapMode; }

    [[nodiscard]] Mipmap mipmap() const { return m_description.mipmap; }
    [[nodiscard]] bool hasMipmaps() const;
    [[nodiscard]] uint32_t mipLevels() const;

    [[nodiscard]] bool isMultisampled() const;
    [[nodiscard]] Multisampling multisampling() const;

    [[nodiscard]] bool hasDepthFormat() const
    {
        return m_description.format == Format::Depth32F || m_description.format == Format::Depth24Stencil8;
    }

    [[nodiscard]] bool hasStencilFormat() const
    {
        return m_description.format == Format::Depth24Stencil8;
    }

    [[nodiscard]] bool hasSrgbFormat() const
    {
        return m_description.format == Format::sRGBA8;
    }

    size_t sizeInMemory() { return m_sizeInMemory; }

protected:
    size_t m_sizeInMemory { SIZE_MAX };

private:
    Description m_description;
};

// Used for storage textures when referencing a specific MIP of the texture 
class TextureMipView {
public:
    TextureMipView(Texture& texture, uint32_t mipLevel)
        : m_texture(&texture)
        , m_mipLevel(mipLevel)
    {
    }

    const Texture& texture() const { return *m_texture; }
    Texture& texture() { return *m_texture; }
    
    uint32_t mipLevel() const { return m_mipLevel; }

private:
    Texture* m_texture { nullptr };
    uint32_t m_mipLevel { 0 };
};

// Define hash functions
namespace std {

    template<>
    struct hash<Texture::Filters> {
        std::size_t operator()(const Texture::Filters& filters) const
        {
            auto minHash = std::hash<Texture::MinFilter>()(filters.min);
            auto magHash = std::hash<Texture::MagFilter>()(filters.mag);
            return hashCombine(minHash, magHash);
        }
    };

    template<>
    struct hash<Texture::WrapModes> {
        std::size_t operator()(const Texture::WrapModes& wrapModes) const
        {
            auto uHash = std::hash<Texture::WrapMode>()(wrapModes.u);
            auto vHash = std::hash<Texture::WrapMode>()(wrapModes.v);
            auto wHash = std::hash<Texture::WrapMode>()(wrapModes.w);
            return hashCombine(uHash, hashCombine(vHash, wHash));
        }
    };

    template<>
    struct hash<Texture::Description> {
        std::size_t operator()(const Texture::Description& desc) const
        {
            // TODO: It would be nice if we had some variadic template function for combining N hashes..
            return hashCombine(std::hash<Texture::Type>()(desc.type),
                               hashCombine(std::hash<uint32_t>()(desc.arrayCount),
                                           hashCombine(std::hash<Extent3D>()(desc.extent),
                                                       hashCombine(std::hash<Texture::Format>()(desc.format),
                                                                   hashCombine(std::hash<Texture::Filters>()(desc.filter),
                                                                               hashCombine(std::hash<Texture::WrapModes>()(desc.wrapMode),
                                                                                           hashCombine(std::hash<Texture::Mipmap>()(desc.mipmap),
                                                                                                       std::hash<Texture::Multisampling>()(desc.multisampling))))))));
        }
    };

} // namespace std

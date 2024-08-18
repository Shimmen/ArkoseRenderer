#pragma once

#include "asset/ImageAsset.h"
#include "rendering/ImageFilter.h"
#include "rendering/ImageWrapMode.h"
#include "rendering/backend/util/ClearValue.h"
#include "rendering/backend/util/IndexType.h"
#include "rendering/backend/Resource.h"
#include "utility/Extent.h"
#include "utility/Hash.h"
#include <array>
#include <cereal/cereal.hpp>
#include <imgui.h>
#include <memory>

class Texture : public Resource {
public:

    // (required for now, so we can create the mockup Texture objects)
    friend class VulkanBackend;
    friend class D3D12Backend;

    enum class Type {
        Texture2D,
        Texture3D,
        Cubemap,
    };

    enum class Format {
        Unknown,
        R8,
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
        R32Uint,
        BC5,
        BC7,
        BC7sRGB,
    };

    // TODO: Move out of Texture to be shared between assets and textures
    enum class MinFilter {
        Linear,
        Nearest,
    };

    // TODO: Move out of Texture to be shared between assets and textures
    enum class MagFilter {
        Linear,
        Nearest,
    };

    // TODO: Move out of Texture to be shared between assets and textures
    // TODO: Also add some option for trilinear here!
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

    static MinFilter convertImageFilterToMinFilter(ImageFilter);
    static MagFilter convertImageFilterToMagFilter(ImageFilter);

    enum class Mipmap {
        None,
        Nearest,
        Linear,
    };

    static Mipmap convertImageFilterToMipFilter(ImageFilter, bool useMipmap);

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
        ImageWrapModes wrapMode { ImageWrapModes::clampAllToEdge() };

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

    static Texture::Format convertImageFormatToTextureFormat(ImageFormat, ImageType);

    static std::unique_ptr<Texture> createFromPixel(Backend&, vec4 pixelColor, bool sRGB);

    // TODO: Remove me, instead just load as an ImageAsset with multiple layers (i.e. depth > 1)
    static std::unique_ptr<Texture> createFromImagePathSequence(Backend&, const std::string& imagePathSequencePattern, bool sRGB, bool generateMipmaps, ImageWrapModes);

    bool hasFloatingPointDataFormat() const;
    virtual bool storageCapable() const = 0;

    virtual void clear(ClearColor) = 0;

    void setPixelData(vec4 pixel);
    virtual void setData(const void* data, size_t size, size_t mipIdx, size_t arrayIdx) = 0;

    virtual void generateMipmaps() = 0;

    Description const& description() const { return m_description; }

    [[nodiscard]] Type type() const { return m_description.type; }

    [[nodiscard]] bool isArray() const { return m_description.arrayCount > 1; };
    [[nodiscard]] uint32_t arrayCount() const { return m_description.arrayCount; };

    [[nodiscard]] const Extent2D extent() const { return { m_description.extent.width(), m_description.extent.height() }; }
    [[nodiscard]] const Extent3D extent3D() const { return m_description.extent; }

    [[nodiscard]] const Extent2D extentAtMip(uint32_t mip) const;
    [[nodiscard]] const Extent3D extent3DAtMip(uint32_t mip) const;

    [[nodiscard]] Format format() const { return m_description.format; }

    [[nodiscard]] MinFilter minFilter() const { return m_description.filter.min; }
    [[nodiscard]] MagFilter magFilter() const { return m_description.filter.mag; }
    [[nodiscard]] Filters filters() const { return m_description.filter; }
    [[nodiscard]] ImageWrapModes wrapMode() const { return m_description.wrapMode; }

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

    // For passing this texture to "Dear ImGui" for rendering
    virtual ImTextureID asImTextureID() = 0;

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
    struct hash<Texture::Description> {
        std::size_t operator()(const Texture::Description& desc) const
        {
            // TODO: It would be nice if we had some variadic template function for combining N hashes..
            return hashCombine(std::hash<Texture::Type>()(desc.type),
                               hashCombine(std::hash<uint32_t>()(desc.arrayCount),
                                           hashCombine(std::hash<Extent3D>()(desc.extent),
                                                       hashCombine(std::hash<Texture::Format>()(desc.format),
                                                                   hashCombine(std::hash<Texture::Filters>()(desc.filter),
                                                                               hashCombine(std::hash<ImageWrapModes>()(desc.wrapMode),
                                                                                           hashCombine(std::hash<Texture::Mipmap>()(desc.mipmap),
                                                                                                       std::hash<Texture::Multisampling>()(desc.multisampling))))))));
        }
    };

} // namespace std

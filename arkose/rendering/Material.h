#pragma once

#include "core/Handle.h"
#include "rendering/backend/Resources.h"
//#include "utility/Image.h"
#include <memory>
#include <ark/vector.h>
#include <string>
#include <optional>
#include <unordered_map>

// Shared shader data
//#include "BlendMode.h"

class Mesh;
class Registry;

DEFINE_HANDLE_TYPE(MaterialHandle)

/*
class Material {
public:
    struct TextureDescription {

        TextureDescription() = default;
        TextureDescription(const TextureDescription&) = default;
        TextureDescription& operator=(const TextureDescription&) = default;
        
        TextureDescription(std::string inPath)
            : path(std::move(inPath))
        {
        }

        TextureDescription(Image inImage)
        {
            image.emplace(std::move(inImage));
        }

        std::string path {};
        std::optional<Image> image {};
        vec4 fallbackColor {};

        bool sRGB { false }; // TODO: Replace with Texture::ColorMode or similar!
        bool mipmapped { true }; // TODO: Use more detailed description (how do we want to filter between mips?)
        ImageWrapModes wrapMode { ImageWrapModes::repeatAll() };
        Texture::Filters filters { Texture::Filters::linear() };

        bool hasPath() const { return !path.empty(); }
        bool hasImage() const { return image.has_value(); }

        bool operator==(const TextureDescription&) const;

        std::string toString() const;
    };

    TextureDescription baseColor {};
    vec4 baseColorFactor { 1.0f };

    TextureDescription normalMap {};
    TextureDescription metallicRoughness {};
    TextureDescription emissive {};

    enum class BlendMode {
        Opaque = BLEND_MODE_OPAQUE,
        Masked = BLEND_MODE_MASKED,
        Translucent = BLEND_MODE_TRANSLUCENT,
    };

    BlendMode blendMode { BlendMode::Opaque };
    int32_t blendModeValue() const { return static_cast<int32_t>(blendMode); }
    float maskCutoff { 1.0f };

    bool isOpaque() const { return blendMode == BlendMode::Opaque; }
};

namespace std {
template<>
struct hash<Material::TextureDescription> {
    std::size_t operator()(const Material::TextureDescription& desc) const
    {
        // NOTE: This must more or less follow the same "pattern" as the equality test!
        auto dataHash = desc.hasPath() ? std::hash<std::string>()(desc.path) : (desc.hasImage() ? std::hash<Image>()(desc.image.value()) : 0u);

        uint64_t fallbackUniqueInt =                   static_cast<uint64_t>(clamp(desc.fallbackColor.x, 0.0f, 1.0f) * 255.99f);
        fallbackUniqueInt = (fallbackUniqueInt << 8) | static_cast<uint64_t>(clamp(desc.fallbackColor.y, 0.0f, 1.0f) * 255.99f);
        fallbackUniqueInt = (fallbackUniqueInt << 8) | static_cast<uint64_t>(clamp(desc.fallbackColor.z, 0.0f, 1.0f) * 255.99f);
        fallbackUniqueInt = (fallbackUniqueInt << 8) | static_cast<uint64_t>(clamp(desc.fallbackColor.w, 0.0f, 1.0f) * 255.99f);
        auto fallbackColorHash = std::hash<uint64_t>()(fallbackUniqueInt);

        auto settingsHash = hashCombine(hashCombine(std::hash<bool>()(desc.sRGB), std::hash<bool>()(desc.mipmapped)),
                                        hashCombine(std::hash<ImageWrapModes>()(desc.wrapMode), std::hash<Texture::Filters>()(desc.filters)));

        return hashCombine(hashCombine(dataHash, fallbackColorHash), settingsHash);
    }
};
}
*/

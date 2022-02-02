#pragma once

#include "backend/Resources.h"
#include "utility/Image.h"
#include <memory>
#include <moos/vector.h>
#include <string>
#include <unordered_map>

// Shared shader data
#include "BlendMode.h"

class Mesh;
class Registry;

class Material {
public:
    struct PathOrImage {
        std::string path;
        std::unique_ptr<Image> image;

        bool hasPath() const { return !path.empty(); }
        bool hasImage() const { return image != nullptr; }
    };

    PathOrImage baseColor {};
    vec4 baseColorFactor { 1.0f };

    PathOrImage normalMap {};
    PathOrImage metallicRoughness {};
    PathOrImage emissive {};

    enum class BlendMode {
        Opaque = BLEND_MODE_OPAQUE,
        Masked = BLEND_MODE_MASKED,
        Translucent = BLEND_MODE_TRANSLUCENT,
    };

    BlendMode blendMode { BlendMode::Opaque };
    int32_t blendModeValue() const { return static_cast<int32_t>(blendMode); }
    float maskCutoff { 1.0f };

    bool isOpaque() const { return blendMode == BlendMode::Opaque; }

    Texture* baseColorTexture();
    Texture* normalMapTexture();
    Texture* metallicRoughnessTexture();
    Texture* emissiveTexture();

private:
    // Texture cache (currently loaded for this material)
    Texture* m_baseColorTexture { nullptr };
    Texture* m_normalMapTexture { nullptr };
    Texture* m_metallicRoughnessTexture { nullptr };
    Texture* m_emissiveTexture { nullptr };
};

class MaterialTextureCache {
public:
    MaterialTextureCache() = default;

    static MaterialTextureCache& global(Badge<Material>);
    static void shutdown(); // HACK!

    Texture* getLoadedTexture(Backend&, const std::string& name, bool sRGB);
    Texture* getTextureForImage(Backend&, const Image&, bool sRGB);
    Texture* getPixelColorTexture(Backend&, vec4 color, bool sRGB);

private:
    static std::unique_ptr<MaterialTextureCache> s_globalCache;

    std::unordered_map<std::string, std::unique_ptr<Texture>> m_loadedTextures;
    std::unordered_map<uint32_t, std::unique_ptr<Texture>> m_pixelColorTextures;
    std::vector<std::unique_ptr<Texture>> m_imageTextures;
};

#include "Material.h"

#include "rendering/Registry.h"
#include "rendering/scene/Mesh.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"
#include "utility/Logging.h"

Texture* Material::baseColorTexture()
{
    if (!m_baseColorTexture) {
        if (baseColor.hasImage()) {
            m_baseColorTexture = MaterialTextureCache::global({}).getTextureForImage(Backend::get(), *baseColor.image, false);
        } else {
            m_baseColorTexture = baseColor.hasPath()
                // FIXME: The comment below applied for glTF 2.0 materials only, so we should standardize it here!
                // (the constant color/factor is already in linear sRGB so we don't want to make an sRGB texture for it)
                ? MaterialTextureCache::global({}).getLoadedTexture(Backend::get(), baseColor.path, true)
                : MaterialTextureCache::global({}).getPixelColorTexture(Backend::get(), baseColorFactor, false);
        }
    }
    return m_baseColorTexture;
}

Texture* Material::normalMapTexture()
{
    if (!m_normalMapTexture) {
        if (normalMap.hasImage()) {
            m_normalMapTexture = MaterialTextureCache::global({}).getTextureForImage(Backend::get(), *normalMap.image, false);
        } else {
            m_normalMapTexture = normalMap.hasPath()
                ? MaterialTextureCache::global({}).getLoadedTexture(Backend::get(), normalMap.path, false)
                : MaterialTextureCache::global({}).getPixelColorTexture(Backend::get(), vec4(0.5f, 0.5f, 1.0f, 1.0f), false);
        }
    }
    return m_normalMapTexture;
}

Texture* Material::metallicRoughnessTexture()
{
    if (!m_metallicRoughnessTexture) {
        if (metallicRoughness.hasImage()) {
            m_metallicRoughnessTexture = MaterialTextureCache::global({}).getTextureForImage(Backend::get(), *metallicRoughness.image, false);
        } else {
            m_metallicRoughnessTexture = metallicRoughness.hasPath()
                ? MaterialTextureCache::global({}).getLoadedTexture(Backend::get(), metallicRoughness.path, false)
                : MaterialTextureCache::global({}).getPixelColorTexture(Backend::get(), { 0, 0, 0, 0 }, true);
        }
    }
    return m_metallicRoughnessTexture;
}

Texture* Material::emissiveTexture()
{
    if (!m_emissiveTexture) {
        if (emissive.hasImage()) {
            m_emissiveTexture = MaterialTextureCache::global({}).getTextureForImage(Backend::get(), *emissive.image, true);
        } else {
            m_emissiveTexture = emissive.hasPath()
                ? MaterialTextureCache::global({}).getLoadedTexture(Backend::get(), emissive.path, true)
                : MaterialTextureCache::global({}).getPixelColorTexture(Backend::get(), { 0, 0, 0, 0 }, true);
        }
    }
    return m_emissiveTexture;
}

std::unique_ptr<MaterialTextureCache> MaterialTextureCache::s_globalCache {};

MaterialTextureCache& MaterialTextureCache::global(Badge<Material>)
{
    if (!s_globalCache)
        s_globalCache = std::make_unique<MaterialTextureCache>();
    return *s_globalCache;
}

void MaterialTextureCache::shutdown()
{
    s_globalCache.reset();
}

Texture* MaterialTextureCache::getLoadedTexture(Backend& backend, const std::string& imagePath, bool sRGB)
{
    // NOTE: This doesn't differentiate between sRGB and not.. likely not an issue in practice though.

    auto entry = m_loadedTextures.find(imagePath);
    if (entry != m_loadedTextures.end())
        return entry->second.get();

    m_loadedTextures[imagePath] = Texture::createFromImagePath(backend, imagePath, sRGB, true, Texture::WrapModes::repeatAll());
    return m_loadedTextures[imagePath].get();
}

Texture* MaterialTextureCache::getTextureForImage(Backend& backend, const Image& image, bool sRGB)
{
    auto texture = Texture::createFromImage(Backend::get(), image, sRGB, true, Texture::WrapModes::repeatAll());
    m_imageTextures.push_back(std::move(texture));
    return m_imageTextures.back().get();
}

Texture* MaterialTextureCache::getPixelColorTexture(Backend& backend, vec4 color, bool sRGB)
{
    // NOTE: If we support HDR/float colors for pixel textures, this key won't be enough..
    //  But right now we still only have RGBA8 or sRGBA8 pixel textures, so this is fine.
    uint32_t key = uint32_t(255.99f * moos::clamp(color.x, 0.0f, 1.0f))
        | (uint32_t(255.99f * moos::clamp(color.y, 0.0f, 1.0f)) << 8)
        | (uint32_t(255.99f * moos::clamp(color.z, 0.0f, 1.0f)) << 16)
        | (uint32_t(255.99f * moos::clamp(color.w, 0.0f, 1.0f)) << 24);

    auto entry = m_pixelColorTextures.find(key);
    if (entry != m_pixelColorTextures.end())
        return entry->second.get();

    m_pixelColorTextures[key] = Texture::createFromPixel(backend, color, sRGB);
    return m_pixelColorTextures[key].get();
}

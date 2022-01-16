#include "Material.h"

#include "rendering/Registry.h"
#include "rendering/scene/Mesh.h"
#include "rendering/scene/Model.h"
#include "rendering/scene/Scene.h"
#include "utility/Logging.h"

Registry& Material::sceneRegistry()
{
    if (!mesh())
        LogErrorAndExit("Material: can't request texture for a material that is not part of a mesh, exiting\n");
    if (!mesh()->model())
        LogErrorAndExit("Material: can't request texture for a mesh's material that is not part of a model, exiting\n");
    if (!mesh()->model()->scene())
        LogErrorAndExit("Material: can't request texture for a mesh's material if it doesn't have a model that is part of a scene, exiting\n");

    Registry& sceneRegistry = mesh()->model()->scene()->registry();
    return sceneRegistry;
}

Texture* Material::baseColorTexture()
{
    if (!m_baseColorTexture) {
        if (baseColor.hasImage()) {
            m_baseColorTexture = &sceneRegistry().createTextureFromImage(*baseColor.image, true, true);
        } else {
            m_baseColorTexture = baseColor.hasPath()
                // FIXME: The comment below applied for glTF 2.0 materials only, so we should standardize it here!
                // (the constant color/factor is already in linear sRGB so we don't want to make an sRGB texture for it)
                ? MaterialTextureCache::global({}).getLoadedTexture(sceneRegistry(), baseColor.path, true)
                : MaterialTextureCache::global({}).getPixelColorTexture(sceneRegistry(), baseColorFactor, false);
        }
    }
    return m_baseColorTexture;
}

Texture* Material::normalMapTexture()
{
    if (!m_normalMapTexture) {
        if (normalMap.hasImage()) {
            m_normalMapTexture = &sceneRegistry().createTextureFromImage(*normalMap.image, false, true);
        } else {
            m_normalMapTexture = normalMap.hasPath()
                ? MaterialTextureCache::global({}).getLoadedTexture(sceneRegistry(), normalMap.path, false)
                : MaterialTextureCache::global({}).getPixelColorTexture(sceneRegistry(), vec4(0.5f, 0.5f, 1.0f, 1.0f), false);
        }
    }
    return m_normalMapTexture;
}

Texture* Material::metallicRoughnessTexture()
{
    if (!m_metallicRoughnessTexture) {
        if (metallicRoughness.hasImage()) {
            m_metallicRoughnessTexture = &sceneRegistry().createTextureFromImage(*metallicRoughness.image, false, true);
        } else {
            m_metallicRoughnessTexture = metallicRoughness.hasPath()
                ? MaterialTextureCache::global({}).getLoadedTexture(sceneRegistry(), metallicRoughness.path, false)
                : MaterialTextureCache::global({}).getPixelColorTexture(sceneRegistry(), { 0, 0, 0, 0 }, true);
        }
    }
    return m_metallicRoughnessTexture;
}

Texture* Material::emissiveTexture()
{
    if (!m_emissiveTexture) {
        if (emissive.hasImage()) {
            m_emissiveTexture = &sceneRegistry().createTextureFromImage(*emissive.image, true, true);
        } else {
            m_emissiveTexture = emissive.hasPath()
                ? MaterialTextureCache::global({}).getLoadedTexture(sceneRegistry(), emissive.path, true)
                : MaterialTextureCache::global({}).getPixelColorTexture(sceneRegistry(), { 0, 0, 0, 0 }, true);
        }
    }
    return m_emissiveTexture;
}

MaterialTextureCache& MaterialTextureCache::global(Badge<Material>)
{
    static std::unique_ptr<MaterialTextureCache> s_globalCache {};
    if (!s_globalCache)
        s_globalCache = std::make_unique<MaterialTextureCache>();
    return *s_globalCache;
}

Texture* MaterialTextureCache::getLoadedTexture(Registry& reg, const std::string& imagePath, bool sRGB)
{
    auto entry = m_loadedTextures.find(imagePath);
    if (entry != m_loadedTextures.end())
        return entry->second;

    Texture& texture = reg.loadTexture2D(imagePath, sRGB, true);
    m_loadedTextures[imagePath] = &texture;

    return &texture;
}

Texture* MaterialTextureCache::getPixelColorTexture(Registry& reg, vec4 color, bool sRGB)
{
    // NOTE: If we support HDR/float colors for pixel textures, this key won't be enough..
    //  But right now we still only have RGBA8 or sRGBA8 pixel textures, so this is fine.
    uint32_t key = uint32_t(255.99f * moos::clamp(color.x, 0.0f, 1.0f))
        | (uint32_t(255.99f * moos::clamp(color.y, 0.0f, 1.0f)) << 8)
        | (uint32_t(255.99f * moos::clamp(color.z, 0.0f, 1.0f)) << 16)
        | (uint32_t(255.99f * moos::clamp(color.w, 0.0f, 1.0f)) << 24);

    auto entry = m_pixelColorTextures.find(key);
    if (entry != m_pixelColorTextures.end())
        return entry->second;

    Texture& texture = reg.createPixelTexture(color, sRGB);
    m_pixelColorTextures[key] = &texture;

    return &texture;
}

#pragma once

#include "backend/Resources.h"
#include <memory>
#include <mooslib/vector.h>
#include <string>
#include <unordered_map>

class Mesh;
class Registry;

class Material {
public:
    std::string baseColor {};
    vec4 baseColorFactor { 1.0f };

    std::string normalMap {};
    std::string metallicRoughness {};
    std::string emissive {};

    void setMesh(Mesh* mesh) { m_owner = mesh; }
    const Mesh* mesh() const { return m_owner; }
    Mesh* mesh() { return m_owner; }

    Texture* baseColorTexture();
    Texture* normalMapTexture();
    Texture* metallicRoughnessTexture();
    Texture* emissiveTexture();

private:
    Mesh* m_owner;
    Registry& sceneRegistry();

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

    Texture* getLoadedTexture(Registry&, const std::string& name, bool sRGB);
    Texture* getPixelColorTexture(Registry&, vec4 color, bool sRGB);

private:
    std::unordered_map<std::string, Texture*> m_loadedTextures;
    std::unordered_map<uint32_t, Texture*> m_pixelColorTextures;
};

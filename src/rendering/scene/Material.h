#pragma once

#include "backend/Resources.h"
#include <mooslib/vector.h>
#include <string>

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

    // Texture cache
    Texture* m_baseColorTexture { nullptr };
    Texture* m_normalMapTexture { nullptr };
    Texture* m_metallicRoughnessTexture { nullptr };
    Texture* m_emissiveTexture { nullptr };
};

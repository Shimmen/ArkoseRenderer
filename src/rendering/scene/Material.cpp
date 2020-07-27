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
        m_baseColorTexture = baseColor.empty()
            // FIXME: The comment below applied for glTF 2.0 materials only, so we should standardize it here!
            // (the constant color/factor is already in linear sRGB so we don't want to make an sRGB texture for it)
            ? &sceneRegistry().createPixelTexture(baseColorFactor, false)
            : &sceneRegistry().loadTexture2D(baseColor, true, true);
    }
    return m_baseColorTexture;
}

Texture* Material::normalMapTexture()
{
    if (!m_normalMapTexture) {
        m_normalMapTexture = normalMap.empty()
            ? &sceneRegistry().loadTexture2D("default-normal.png", false, false)
            : &sceneRegistry().loadTexture2D(normalMap, false, true);
    }
    return m_normalMapTexture;
}

Texture* Material::metallicRoughnessTexture()
{
    if (!m_metallicRoughnessTexture) {
        m_metallicRoughnessTexture = metallicRoughness.empty()
            ? &sceneRegistry().createPixelTexture({ 0, 0, 0, 0 }, false)
            : &sceneRegistry().loadTexture2D(metallicRoughness, false, true);
    }
    return m_metallicRoughnessTexture;
}

Texture* Material::emissiveTexture()
{
    if (!m_emissiveTexture) {
        m_emissiveTexture = emissive.empty()
            ? &sceneRegistry().createPixelTexture({ 0, 0, 0, 0 }, true)
            : &sceneRegistry().loadTexture2D(emissive, true, true);
    }
    return m_emissiveTexture;
}

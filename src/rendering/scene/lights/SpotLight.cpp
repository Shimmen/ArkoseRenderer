#include "SpotLight.h"

#include "utility/Logging.h"

SpotLight::SpotLight(vec3 color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction)
    : Light(Type::SpotLight, color)
    , luminousIntensity(luminousIntensity)
    , m_iesProfile(iesProfilePath)
    , m_position(position)
    , m_direction(normalize(direction))
{
    // NOTE: Feel free to adjust these on a per-light/case basis, but probably in the scene.json
    customConstantBias = 1.0f;
    customSlopeBias = 0.66f;

    // Good default for spot lights
    setShadowMapSize({ 512, 512 });
}

Texture& SpotLight::iesProfileLookupTexture()
{
    if (!scene())
        LogErrorAndExit("SpotLight: can't request IES profile LUT for light that is not part of a scene, exiting\n");

    // TODO: Cache these!! Both loading of IES profiles & the LUTs
    return m_iesProfile.createLookupTexture(*scene(), SpotLightIESLookupTextureSize);
}

float SpotLight::constantBias()
{
    int maxShadowMapDim = std::max(shadowMapSize().width(), shadowMapSize().height());
    return 0.1f * customConstantBias / float(maxShadowMapDim);
}

float SpotLight::slopeBias()
{
    return customSlopeBias * constantBias();
}
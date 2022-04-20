#include "SpotLight.h"

#include "backend/base/Backend.h"

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

float SpotLight::constantBias()
{
    int maxShadowMapDim = std::max(shadowMapSize().width(), shadowMapSize().height());
    return 0.1f * customConstantBias / float(maxShadowMapDim);
}

float SpotLight::slopeBias()
{
    return customSlopeBias * constantBias();
}
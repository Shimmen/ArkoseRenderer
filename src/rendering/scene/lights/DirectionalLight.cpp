#include "DirectionalLight.h"

DirectionalLight::DirectionalLight(vec3 color, float illuminance, vec3 direction)
    : Light(Type::DirectionalLight, color)
    , illuminance(illuminance)
    , direction(normalize(direction))
    , shadowMapWorldOrigin(0, 0, 0)
    , shadowMapWorldExtent(50.0f)
{
    // NOTE: Feel free to adjust these on a per-light/case basis, but probably in the scene.json
    customConstantBias = 3.5f;
    customSlopeBias = 2.5f;
}

float DirectionalLight::constantBias()
{
    int maxShadowMapDim = std::max(shadowMapSize().width(), shadowMapSize().height());
    float worldTexelScale = shadowMapWorldExtent / maxShadowMapDim;

    // For the projection we use [-extent/2, +extent/2] for near & far so the full extent is the depth range
    float worldDepthRange = shadowMapWorldExtent;

    float bias = customConstantBias * worldTexelScale / worldDepthRange;
    return bias;
}

float DirectionalLight::slopeBias()
{
    return 0.1f * customSlopeBias * constantBias();
}

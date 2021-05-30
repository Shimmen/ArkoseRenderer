#pragma once

#include "Light.h"
#include <moos/transform.h>

class DirectionalLight final : public Light {
public:
    DirectionalLight() = default;
    DirectionalLight(vec3 color, float illuminance, vec3 direction)
        : Light(Type::DirectionalLight, color)
        , illuminance(illuminance)
        , direction(normalize(direction))
        , shadowMapWorldOrigin(0, 0, 0)
        , shadowMapWorldExtent(50.0f)
    {
        constantBias = 0.0003f;
        slopeBias = 0.0f;
    }

    virtual ~DirectionalLight() { }

    float intensityValue() const final
    {
        return illuminance;
    }

    vec3 forwardDirection() const final
    {
        return direction;
    }

    mat4 lightViewMatrix() const final
    {
        return moos::lookAt(shadowMapWorldOrigin, shadowMapWorldOrigin + normalize(direction));
    }

    mat4 projectionMatrix() const final
    {
        return moos::orthographicProjectionToVulkanClipSpace(shadowMapWorldExtent, -shadowMapWorldExtent, shadowMapWorldExtent);
    }

    // Light illuminance (lux, lx = lm / m^2)
    // TODO: Actually use physically based units!
    float illuminance { 1.0f };

    // Direction of outgoing light, i.e. -L in a BRDF
    vec3 direction { 1, 1, 1 };

    // When rendering a shadow map, from what point in the world should it be rendered from
    vec3 shadowMapWorldOrigin { 0, 0, 0 };

    // When rendering a shadow map, how much of the scene around it should it cover (area, relative to direction)
    float shadowMapWorldExtent { 50.0f };
};

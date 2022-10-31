#pragma once

#include "Light.h"
#include <ark/transform.h>

class DirectionalLight final : public Light {
public:
    DirectionalLight();
    DirectionalLight(vec3 color, float illuminance, vec3 direction);
    virtual ~DirectionalLight() { }

    template<class Archive>
    void serialize(Archive&);

    float intensityValue() const final
    {
        return illuminance;
    }

    mat4 projectionMatrix() const final
    {
        return ark::orthographicProjectionToVulkanClipSpace(shadowMapWorldExtent, -0.5f * shadowMapWorldExtent, 0.5f * shadowMapWorldExtent);
    }

    virtual float constantBias(Extent2D shadowMapSize) const override;
    virtual float slopeBias(Extent2D shadowMapSize) const override;

    // Light illuminance (lux, lx = lm / m^2)
    // TODO: Actually use physically based units!
    float illuminance { 1.0f };

    // When rendering a shadow map, from what point in the world should it be rendered from
    static constexpr vec3 shadowMapWorldOrigin { 0, 0, 0 };

    // When rendering a shadow map, how much of the scene around it should it cover (area, relative to direction)
    static constexpr float shadowMapWorldExtent { 50.0f };
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

CEREAL_REGISTER_TYPE(DirectionalLight)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Light, DirectionalLight)

template<class Archive>
void DirectionalLight::serialize(Archive& archive)
{
    archive(cereal::make_nvp("Light", cereal::base_class<Light>(this)));

    archive(cereal::make_nvp("illuminance", illuminance));
}

#pragma once

#include "scene/Transform.h"
#include "scene/lights/Light.h"
#include "utility/IESProfile.h"
#include <ark/transform.h>

class SphereLight final : public Light {
public:
    SphereLight();
    SphereLight(vec3 color, float luminousPower, vec3 position, float lightSourceRadius);
    virtual ~SphereLight() { }

    template<class Archive>
    void serialize(Archive&);

    float intensityValue() const final
    {
        return luminousPower;
    }

    // Light luminous power/flux (lumen)
    // TODO: Actually use physically based units!
    float luminousPower { 1.0f };

    // Radius of the sphere light source (counted from the center of the sphere)
    float lightSourceRadius { 0.1f };

    // No shadow mapping for sphere lights, only ray traced shadows
    virtual mat4 projectionMatrix() const override { return mat4(); }
    virtual float constantBias(Extent2D shadowMapSize) const override { return 0.0f; }
    virtual float slopeBias(Extent2D shadowMapSize) const override { return 0.0f; }
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

CEREAL_REGISTER_TYPE(SphereLight)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Light, SphereLight)

template<class Archive>
void SphereLight::serialize(Archive& archive)
{
    archive(cereal::make_nvp("Light", cereal::base_class<Light>(this)));

    archive(cereal::make_nvp("luminousPower", luminousPower));
    archive(cereal::make_nvp("lightSourceRadius", lightSourceRadius));
}
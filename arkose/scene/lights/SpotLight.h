#pragma once

#include "scene/lights/Light.h"
#include "scene/Transform.h"
#include "utility/IESProfile.h"
#include <ark/transform.h>

class SpotLight final : public Light {
public:
    SpotLight();
    SpotLight(vec3 color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction);
    virtual ~SpotLight() { }

    template<class Archive>
    void serialize(Archive&);

    float intensityValue() const final
    {
        return luminousIntensity;
    }

    mat4 projectionMatrix() const final
    {
        return ark::perspectiveProjectionToVulkanClipSpace(outerConeAngle, 1.0f, m_zNear, m_zFar);
    }

    virtual float constantBias(Extent2D shadowMapSize) const override;
    virtual float slopeBias(Extent2D shadowMapSize) const override;

    bool hasIesProfile() const { return true; }
    const IESProfile& iesProfile() const { return m_iesProfile; }

    static constexpr int IESLookupTextureSize = 256;

    // Light luminous intensity (candelas)
    // TODO: Actually use physically based units!
    float luminousIntensity { 1.0f };

    // This will scale the IES profile so that it fits within the given angle
    float outerConeAngle { ark::toRadians(120.0f) };

private:

    IESProfile m_iesProfile {};
    std::unique_ptr<Texture> m_iesLookupTexture {};

    static constexpr float m_zNear { 0.1f };
    static constexpr float m_zFar { 1000.0f };

};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/types/polymorphic.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

CEREAL_REGISTER_TYPE(SpotLight)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Light, SpotLight)

template<class Archive>
void SpotLight::serialize(Archive& archive)
{
    archive(cereal::make_nvp("Light", cereal::base_class<Light>(this)));

    archive(cereal::make_nvp("luminousIntensity", luminousIntensity));

    archive(cereal::make_nvp("outerConeAngle", outerConeAngle));
    archive(cereal::make_nvp("IESProfile", m_iesProfile));
}

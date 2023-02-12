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

    // IEditorObject interface
    virtual void drawGui() override;

    float intensityValue() const final { return m_luminousIntensity; }

    float outerConeAngle() const { return m_outerConeAngle; }

    mat4 projectionMatrix() const final;
    virtual float constantBias(Extent2D shadowMapSize) const override;
    virtual float slopeBias(Extent2D shadowMapSize) const override;

    bool hasIesProfile() const { return true; }
    const IESProfile& iesProfile() const { return m_iesProfile; }

    static constexpr int IESLookupTextureSize = 256;

private:

    IESProfile m_iesProfile {};
    std::unique_ptr<Texture> m_iesLookupTexture {};

    // Light luminous intensity (candelas)
    // TODO: Actually use physically based units!
    float m_luminousIntensity { 1.0f };

    // This will scale the IES profile so that it fits within the given angle
    float m_outerConeAngle { ark::toRadians(120.0f) };

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

    archive(cereal::make_nvp("luminousIntensity", m_luminousIntensity));

    archive(cereal::make_nvp("outerConeAngle", m_outerConeAngle));
    archive(cereal::make_nvp("IESProfile", m_iesProfile));
}

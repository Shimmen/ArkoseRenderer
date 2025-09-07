#pragma once

#include "asset/external/IESProfile.h"
#include "scene/lights/Light.h"
#include "scene/Transform.h"
#include <ark/color.h>
#include <ark/transform.h>

class SpotLight final : public Light {
public:
    SpotLight();
    SpotLight(LightAsset const&);
    SpotLight(Color color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction);
    virtual ~SpotLight() { }

    // IEditorObject interface
    virtual void drawGui() override;

    float intensityValue() const final { return m_luminousIntensity; }

    virtual bool supportsShadowMode(ShadowMode) const override;

    float outerConeAngle() const { return m_outerConeAngle; }

    mat4 projectionMatrix() const final;
    virtual float constantBias() const override;
    virtual float slopeBias() const override;

    bool hasIesProfile() const { return true; }
    const IESProfile& iesProfile() const { return m_iesProfile; }

    float lightSourceRadius() const { return m_lightSourceRadius; }
    void setLightSourceRadius(float radius) { m_lightSourceRadius = radius; }

private:

    IESProfile m_iesProfile {};
    std::unique_ptr<Texture> m_iesLookupTexture {};

    // Light luminous intensity (candelas)
    // TODO: Actually use physically based units!
    float m_luminousIntensity { 1.0f };

    // Radius of the light source (sphere)
    float m_lightSourceRadius { 0.025f };

    // This will scale the IES profile so that it fits within the given angle
    float m_outerConeAngle { ark::toRadians(120.0f) };

    static constexpr float m_zNear { 0.1f };
    static constexpr float m_zFar { 1000.0f };

};

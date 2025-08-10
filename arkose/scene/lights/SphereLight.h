#pragma once

#include "scene/Transform.h"
#include "scene/lights/Light.h"
#include <ark/color.h>
#include <ark/transform.h>

class SphereLight final : public Light {
public:
    SphereLight();
    SphereLight(LightAsset const&);
    SphereLight(Color color, float luminousPower, vec3 position, float lightSourceRadius);
    virtual ~SphereLight() { }

    // IEditorObject interface
    virtual void drawGui() override;

    virtual float intensityValue() const final;

    virtual bool supportsShadowMode(ShadowMode) const override;

    float lightRadius() const { return m_lightRadius; }
    float lightSourceRadius() const { return m_lightSourceRadius; }

    // No shadow mapping for sphere lights, only ray traced shadows
    virtual mat4 projectionMatrix() const override { return mat4(); }
    virtual float constantBias() const override { return 0.0f; }
    virtual float slopeBias() const override { return 0.0f; }

private:
    void updateLightRadius();

    // Light luminous power/flux (lumen)
    // TODO: Actually use physically based units!
    float m_luminousPower { 1.0f };

    // Radius of the lighting influence of this light (the radius of effect)
    float m_lightRadius { 10.0f };

    // Radius of the spherical light source
    float m_lightSourceRadius { 0.05f };

};

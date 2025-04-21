#pragma once

#include "Light.h"

#include <ark/color.h>
#include <ark/transform.h>

class DirectionalLight final : public Light {
public:
    DirectionalLight();
    DirectionalLight(LightAsset const&);
    DirectionalLight(Color color, float illuminance, vec3 direction);
    virtual ~DirectionalLight() { }

    float illuminance() const { return m_illuminance; }
    void setIlluminance(float illuminance) { m_illuminance = illuminance; }

    // IEditorObject interface
    virtual void drawGui() override;

    float intensityValue() const final { return illuminance(); }

    mat4 projectionMatrix() const final;
    virtual float constantBias() const override;
    virtual float slopeBias() const override;

    // When rendering a shadow map, from what point in the world should it be rendered from
    static constexpr vec3 shadowMapWorldOrigin { 0, 0, 0 };

    // When rendering a shadow map, how much of the scene around it should it cover (area, relative to direction)
    float shadowMapWorldExtent { 175.0f };

private:
    // Light illuminance (lux, lx = lm / m^2)
    // TODO: Actually use physically based units!
    float m_illuminance { 1.0f };
};

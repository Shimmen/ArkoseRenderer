#pragma once

#include "rendering/scene/lights/Light.h"
#include "rendering/scene/Transform.h"
#include "utility/IESProfile.h"
#include <moos/transform.h>

class SpotLight final : public Light {
public:
    SpotLight() = default;
    SpotLight(vec3 color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction);
    virtual ~SpotLight() { }

    vec3 position() const final
    {
        return m_position;
    }

    float intensityValue() const final
    {
        return luminousIntensity;
    }

    vec3 forwardDirection() const final
    {
        return normalize(m_direction);
    }

    mat4 lightViewMatrix() const final
    {
        return moos::lookAt(m_position, m_position + forwardDirection());
    }

    mat4 projectionMatrix() const final
    {
        return moos::perspectiveProjectionToVulkanClipSpace(outerConeAngle, 1.0f, m_zNear, m_zFar);
    }

    virtual float constantBias() override;
    virtual float slopeBias() override;

    bool hasIesProfile() const { return true; }
    const IESProfile& iesProfile() const { return m_iesProfile; }

    static constexpr int IESLookupTextureSize = 256;

    // Light luminous intensity (candelas)
    // TODO: Actually use physically based units!
    float luminousIntensity { 1.0f };

    // This will scale the IES profile so that it fits within the given angle
    float outerConeAngle { moos::toRadians(120.0f) };

private:

    IESProfile m_iesProfile {};
    std::unique_ptr<Texture> m_iesLookupTexture {};

    vec3 m_position { 0, 0, 0 };
    vec3 m_direction { 1, 1, 1 };

    float m_zNear { 0.1f };
    float m_zFar { 1000.0f };

};

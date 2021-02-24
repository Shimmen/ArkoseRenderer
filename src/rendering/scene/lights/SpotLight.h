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

    virtual mat4 viewProjection() const final;

    float outerConeAngle() const;

    // Light luminous intensity (candelas)
    // TODO: Actually use physically based units!
    float luminousIntensity { 1.0f };

private:

    IESProfile m_iesProfile {};

    vec3 m_position { 0, 0, 0 };
    vec3 m_direction { 1, 1, 1 };

};

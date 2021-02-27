#include "SpotLight.h"

#include "utility/Logging.h"

SpotLight::SpotLight(vec3 color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction)
    : Light(Type::SpotLight, color)
    , luminousIntensity(luminousIntensity)
    , m_iesProfile(iesProfilePath)
    , m_position(position)
    , m_direction(direction)
{
}

mat4 SpotLight::viewProjection() const
{
    mat4 lightOrientation = moos::lookAt(m_position, m_position + normalize(m_direction));
    mat4 lightProjection = moos::perspectiveProjectionToVulkanClipSpace(outerConeAngle(), 1.0f, 0.1f, 1000.0f);
    return lightProjection * lightOrientation;
}

float SpotLight::outerConeAngle() const
{
    // TODO: Actually look to the IES profile!
    //float requiredAngle = m_iesProfile.requiredSpotLightConeAngle();
    float requiredAngle = moos::HALF_PI;

    if (requiredAngle >= moos::PI - 1e-2f) {
        LogWarning("SpotLight: outer cone angle from .ies-file '%s' does not work for a spot light, contricting angle for shadows\n", m_iesProfile.path().c_str());
        return moos::PI - 1e-2f;
    }

    return requiredAngle;
}

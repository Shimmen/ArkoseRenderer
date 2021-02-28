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
    mat4 lightProjection = moos::perspectiveProjectionToVulkanClipSpace(outerConeAngle, 1.0f, 0.1f, 1000.0f);
    return lightProjection * lightOrientation;
}

Texture& SpotLight::iesProfileLookupTexture()
{
    if (!scene())
        LogErrorAndExit("SpotLight: can't request IES profile LUT for light that is not part of a scene, exiting\n");

    // TODO: Cache these!! Both loading of IES profiles & the LUTs
    return m_iesProfile.createLookupTexture(*scene(), SpotLightIESLookupTextureSize);
}

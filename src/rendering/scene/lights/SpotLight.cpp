#include "SpotLight.h"

#include "utility/Logging.h"

SpotLight::SpotLight(vec3 color, float luminousIntensity, const std::string& iesProfilePath, vec3 position, vec3 direction)
    : Light(Type::SpotLight, color)
    , luminousIntensity(luminousIntensity)
    , m_iesProfile(iesProfilePath)
    , m_position(position)
    , m_direction(direction)
{
    constantBias = 0.0001f;
    slopeBias = 0.0f;
}

Texture& SpotLight::iesProfileLookupTexture()
{
    if (!scene())
        LogErrorAndExit("SpotLight: can't request IES profile LUT for light that is not part of a scene, exiting\n");

    // TODO: Cache these!! Both loading of IES profiles & the LUTs
    return m_iesProfile.createLookupTexture(*scene(), SpotLightIESLookupTextureSize);
}

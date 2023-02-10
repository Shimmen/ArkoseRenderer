#include "SphereLight.h"

#include "core/Assert.h"

SphereLight::SphereLight()
    : Light(Type::SphereLight, vec3(1.0f))
{
}

SphereLight::SphereLight(vec3 color, float inLuminousPower, vec3 position, float inLightSourceRadius)
    : Light(Type::SphereLight, color)
    , luminousPower(inLuminousPower)
    , lightSourceRadius(inLightSourceRadius)
{
    transform().setPositionInWorld(position);
    lightSourceRadius = std::max(1e-4f, lightSourceRadius);
}

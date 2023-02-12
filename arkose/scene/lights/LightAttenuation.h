#pragma once

namespace LightAttenuation {

float calculatePhysicallyBasedLightAttenuation(float distanceToLightSource);
float calculateModulatedLightAttenuation(float distanceToLightSource, float lightSourceRadius, float lightRadius);

float calculateAbsoluteErrorDueToModulatedFunction(float distanceToLightSource, float lightSourceRadius, float lightRadius);
float calculateSmallestLightRadius(float lightSourceRadius, float maxError);

}

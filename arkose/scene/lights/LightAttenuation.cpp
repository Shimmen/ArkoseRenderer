#include "LightAttenuation.h"

#include "core/Assert.h"
#include "core/Types.h"
#include "utility/Profiling.h"
#include <ark/core.h>

float LightAttenuation::calculatePhysicallyBasedLightAttenuation(float distanceToLightSource)
{
    ARKOSE_ASSERT(distanceToLightSource > 1e-6f);
    return 1.0f / ark::square(distanceToLightSource);
}

float LightAttenuation::calculateModulatedLightAttenuation(float distanceToLightSource, float lightSourceRadius, float lightRadius)
{
    float const& s = lightSourceRadius;
    float const& r = lightRadius;

    float const d = ark::clamp(distanceToLightSource, s, r);

    float numerator = std::cos(ark::PI * (d - s) / (r - s)) + 1;
    float denominator = 2.0 * ark::square(d);
    return numerator / denominator;
}

float LightAttenuation::calculateAbsoluteErrorDueToModulatedFunction(float distanceToLightSource, float lightSourceRadius, float lightRadius)
{
    // Consider error to be zero when outside of the (modulated) function's domain
    if (distanceToLightSource <= lightSourceRadius || distanceToLightSource > lightRadius + 1e-4f) {
        return 0.0f;
    }

    float physicallyBased = calculatePhysicallyBasedLightAttenuation(distanceToLightSource);
    float modulated = calculateModulatedLightAttenuation(distanceToLightSource, lightSourceRadius, lightRadius);

    return std::abs(physicallyBased - modulated);
}

float LightAttenuation::calculateSmallestLightRadius(float lightSourceRadius, float maxError)
{
    SCOPED_PROFILE_ZONE();

    // Ensure error is not unrealistically small
    ARKOSE_ASSERT(maxError > 1e-6f);

    float minRadius = lightSourceRadius + 0.01f;
    float maxRadius = 10'000.0f;

    u32 numIterations = 0;
    constexpr u32 MaxNumIterations = 20;

    // Any error below the max error is valid, but we want to ensure we get the tighest possible radius.
    // Therefore, we would like to stay within a single error worth of margin below the max error.
    const float MaxAllowedErrorMargin = 0.1f * maxError;

    while (numIterations < MaxNumIterations) {

        float testRadius = (minRadius + maxRadius) / 2.0f;
        float evalDistance = testRadius; // Evaluate the error at the test radius

        float error = calculateAbsoluteErrorDueToModulatedFunction(evalDistance, lightSourceRadius, testRadius);

        if (error < maxError) {
            // Check if we have a valid error within the margin
            float errorMargin = maxError - error;
            if (errorMargin <= MaxAllowedErrorMargin) {
                return testRadius;
            }

            // Error is valid, but we can get closer
            maxRadius = testRadius;
        } else {
            // Error is too large, we have to try larger radii
            minRadius = testRadius;
        }

        numIterations += 1;
    }

    // After max iterations we failed to find a suitable radius. Use the max radius.
    return maxRadius;
}

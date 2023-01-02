#ifndef IES_PROFILE_GLSL
#define IES_PROFILE_GLSL

#include <common.glsl>
#include <shared/LightData.h>

float evaluateIESLookupTable(sampler2D iesLUT, float outerConeHalfAngle, float cosAngle/*, theOtherAngle*/)
{
    if (cosAngle <= 0.0) {
        return 0.0;
    }

    // TODO: This is not 100% correct..
    // And we're only evaluating one of the angles, obviously..
    float x = acos(cosAngle) / (2.0 * outerConeHalfAngle);
    float y = 0.5;

    vec2 lookup = vec2(x, y);

    return textureLod(iesLUT, lookup, 0.0).r;
}

#endif // IES_PROFILE_GLSL

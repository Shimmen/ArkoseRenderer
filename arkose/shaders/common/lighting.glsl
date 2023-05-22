#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include <common.glsl>

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

//
// We model a light source as a sphere centered at point X, with light source radius s, and lighting radius r (i.e., the radius of effect for the light)
// The physically accurate falloff/attenuation function is 1/d^2, but it has two problems which my function solves:
//
//  1) 1/d^2 goes to infinity as distance d approaches zero. To solve this, we assume we can never get closer to the light source than the distance s,
//     thus d >= s, and as we ensure s > 0 we will never get to infinity.
//
//  2) 1/d^2 goes to zero as distance d approches infinity, but it will never actually arrive at zero. This makes it impossible to accurately cull lights
//     with some kind of radius r. We modulate the function 1/d^2 with a cosine term which is 1 where d=s and 0 where d=r. The modulating factor is:
//
//               /   d - s  \ 
//       z = cos |π --------| * 0.5 + 0.5
//               \   r - s  / 
//    
//     While its 0 where d=r the same is not true for d>r, thus we also need to clamp d<=r.
//
// Multiplying the physically based falloff 1/d^2 with z gives us:
//
//       /   d - s  \    
//   cos |π --------| + 1
//       \   r - s  /    
//  ----------------------
//               2      
//          2 . d       
// 
// where s > 0, r > s, and d is clamped to the range [s, r].
//
float calculateLightDistanceAttenuation(float distanceToLightSource, float lightSourceRadius, float lightRadius)
{
    const float s = lightSourceRadius;
    const float r = lightRadius;

    const float d = clamp(distanceToLightSource, s, r);

    float numerator = cos(PI * (d - s) / (r - s)) + 1;
    float denominator = 2.0 * square(d);
    return numerator / denominator;
}

#endif // LIGHTING_GLSL

#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

#include <common.glsl>
#include <shared/LightData.h>

// Corresponding to published binding set "SceneLightSet"
#define DeclareCommonBindingSet_Light(index)                                                                              \
    layout(set = index, binding = 0) uniform         LightMetaDataBlock   { LightMetaData        _lightMeta; };           \
    layout(set = index, binding = 1) buffer readonly DirLightDataBlock    { DirectionalLightData _directionalLight; };    \
    layout(set = index, binding = 2) buffer readonly SpotLightDataBlock   { SpotLightData        _spotLights[]; };

#define light_hasDirectionalLight() _lightMeta.hasDirectionalLight
#define light_getSpotLightCount() _lightMeta.numSpotLights

#define light_getDirectionalLight() _directionalLight
#define light_getSpotLight(index) _spotLights[index]


float evaluateIESLookupTable(sampler2D iesLUT, float outerConeHalfAngle, mat3 lightMatrix, vec3 lightRayDir)
{
    float angleV = dot(lightRayDir, lightMatrix[2]);

    // NOTE: Since this light is shadow mapped we can't handle angles >=90 so we might as well return black for those.
    // If we use ray-traced shadows we can support any angle here and don't need to consider this, or the outer/max angle.
    if (angleV <= 0.0) {
        return 0.0;
    }

    float hx = dot(lightRayDir, lightMatrix[0]);
    float hy = dot(lightRayDir, lightMatrix[1]);
    float angleH = atan(hy, hx) + PI;

    vec2 lookup;
    lookup.x = acos(angleV) / (2.0 * outerConeHalfAngle);
    lookup.y = clamp(angleH / TWO_PI, 0.0, 1.0);

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

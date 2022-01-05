#ifndef FILMGRAIN_GLSL
#define FILMGRAIN_GLSL

#include <common/noise.glsl>

vec3 generateFilmGrain(float gain, uint frameIdx, uvec2 pixelCoord, uvec2 targetSize)
{
    // TODO: Use blue noise (or something even better)
    // TODO: Make filmGrainGain a function of the camera ISO: higher ISO -> more digital sensor noise!
    float noise = hash_2u_to_1f(pixelCoord + frameIdx * targetSize);
    return vec3(gain * (2.0 * noise - 1.0));
}

vec3 applyFilmGrain(vec3 color, vec3 filmGrain)
{
    return clamp(color + filmGrain, vec3(0.0), vec3(1.0));
}

#endif // FILMGRAIN_GLSL

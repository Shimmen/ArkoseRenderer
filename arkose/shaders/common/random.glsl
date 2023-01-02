#ifndef RANDOM_GLSL
#define RANDOM_GLSL

#include <common.glsl>

// Most of this comes from http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/

uint _rng_state;

uint rand_lcg()
{
    // LCG values from Numerical Recipes
    _rng_state = 1664525 * _rng_state + 1013904223;
    return _rng_state;
}

uint rand_xorshift()
{
    // Xorshift algorithm from George Marsaglia's paper
    _rng_state ^= (_rng_state << 13);
    _rng_state ^= (_rng_state >> 17);
    _rng_state ^= (_rng_state << 5);
    return _rng_state;
}

uint wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

/////////////////////////////////
// Convenience API:

void seedRandom(uint seed)
{
    _rng_state = wang_hash(seed);
}

float randomFloat()
{
    return float(rand_xorshift()) * (1.0 / 4294967296.0);
}

vec3 randomPointOnSphere()
{
    // Source: https://mathworld.wolfram.com/SpherePointPicking.html
    float theta = TWO_PI * randomFloat();
    float u = 2.0 * randomFloat() - 1.0;
    float sr = sqrt(1.0 - u * u);
    return vec3(
        sr * cos(theta),
        sr * sin(theta),
        u
    );
}

#endif // RANDOM_GLSL

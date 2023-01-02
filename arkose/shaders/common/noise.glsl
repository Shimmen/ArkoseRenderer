#ifndef NOISE_GLSL
#define NOISE_GLSL

// From https://www.shadertoy.com/view/4tXyWN
float hash_2u_to_1f(uvec2 x)
{
    uvec2 q = 1103515245u * ((x >>1U) ^ (x.yx));
    uint n = 1103515245u * ((q.x) ^ (q.y>>3U));
    return float(n) * (1.0 / float(0xffffffffu));
}

#endif // NOISE_GLSL

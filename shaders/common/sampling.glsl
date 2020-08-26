#ifndef SAMPLING_GLSL
#define SAMPLING_GLSL

vec4 bilinearFilter(vec4 tl, vec4 tr, vec4 bl, vec4 br, vec2 frac)
{
    frac = fract(frac);
    vec4 top = mix(tl, tr, frac.x);
    vec4 bottom = mix(bl, br, frac.x);
    return mix(bottom, top, frac.y);
}

#endif // SAMPLING_GLSL

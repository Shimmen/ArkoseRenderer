#ifndef AGX_GLSL
#define AGX_GLSL

//
// This code is based off of "Minimal AgX Implementation" by Benjamin Wrensch (licensed under the MIT license):
// https://iolite-engine.com/blog_posts/minimal_agx_implementation
//
// which states
//
// "All values used to derive this implementation are sourced from Troy’s initial AgX implementation/OCIO config file available here:"
//   https://github.com/sobotka/AgX
//

// sRGB => AgX
const mat3 AGX_input_matrix = mat3(
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104
);

// AgX => sRGB
const mat3 AGX_output_matrix = mat3(
    1.19687900512017,    -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433,  1.15107367264116
);

// 0: Default, 1: Golden, 2: Punchy
#define AGX_LOOK 0

#if 1
// Mean error^2: 3.6705141e-06
vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;

    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}
#else
// Mean error^2: 1.85907662e-06
vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    vec3 x6 = x4 * x2;

    return - 17.86     * x6 * x
           + 78.01     * x6
           - 126.7     * x4 * x
           + 92.06     * x4
           - 28.72     * x2 * x
           + 4.361     * x2
           - 0.1718    * x
           + 0.002857;
}
#endif

vec3 agx(vec3 val) {

    // Input transform (inset)
    // TODO(simon): Apply this on material texture inputs & other input colors
    //              and work in this space instead of just doing it here.
    val = AGX_input_matrix * val;

    #if 1
    const float blackPoint = 0.00017578139;
    const float whitePoint = 16.2917423653;
    const float minEv = log2(blackPoint);
    const float maxEv = log2(whitePoint);
    #else
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    #endif

    // Log2 space encoding
    val = clamp(log2(val), minEv, maxEv);
    val = (val - minEv) / (maxEv - minEv);
  
    // Apply sigmoid function approximation
    val = agxDefaultContrastApprox(val);

    return val;
}

vec3 agxEotf(vec3 val) {
    // Inverse input transform (outset)
    val = AGX_output_matrix * val;
  
    // sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
    // NOTE: We're linearizing the output here. Comment/adjust when
    // *not* using a sRGB render target
    // NOTE(simon): We're assuming linear output for all our tonemap methods,
    //              as we will apply the nonlinear transformation ourselves later.
    val = pow(val, vec3(2.2));

    return val;
}

vec3 agxLook(vec3 val) {
    const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
    float luma = dot(val, lw);

    // Default
    vec3 offset = vec3(0.0);
    vec3 slope = vec3(1.0);
    vec3 power = vec3(1.0);
    float saturation = 1.0;
 
#if AGX_LOOK == 1
    // Golden
    slope = vec3(1.0, 0.9, 0.5);
    power = vec3(0.8);
    saturation = 0.8;
#elif AGX_LOOK == 2
    // Punchy (ACES-like)
    slope = vec3(1.0);
    power = vec3(1.35, 1.35, 1.35);
    saturation = 1.4;
#endif

    // ASC CDL
    val = pow(val * slope + offset, power);
    return luma + saturation * (val - luma);
}

vec3 AgX_tonemap(vec3 color)
{
    color = agx(color);
    color = agxLook(color);
    return agxEotf(color);
}

#endif // AGX_GLSL

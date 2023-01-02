#ifndef ACES_GLSL
#define ACES_GLSL

//
// This code is modified from 'Baking Lab' by MJP and David Neubelt (licensed under the MIT license):
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
//
// which states
//
// "The code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)"
//

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACES_input_matrix = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACES_output_matrix = mat3(
    1.60475, -0.10208, -0.00327,
    -0.53108, 1.10813, -0.07276,
    -0.07367, -0.00605, 1.07602
);

vec3 ACES_RRTAndODTFit(vec3 v)
{
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACES_tonemap(vec3 color)
{
    color = ACES_input_matrix * color;

    // Apply RRT and ODT
    color = ACES_RRTAndODTFit(color);

    color = ACES_output_matrix * color;

    // Clamp to [0, 1]
    color = clamp(color, vec3(0.0), vec3(1.0));

    return color;
}

#endif// ACES_GLSL

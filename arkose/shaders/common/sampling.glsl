#ifndef SAMPLING_GLSL
#define SAMPLING_GLSL

#include <common.glsl>

vec4 bilinearFilter(vec4 tl, vec4 tr, vec4 bl, vec4 br, vec2 frac)
{
    vec4 top = mix(tl, tr, frac.x);
    vec4 bottom = mix(bl, br, frac.x);
    return mix(top, bottom, frac.y);
}

////////////////////////////////////////////////////////////////////////////////
// NOTE(Simon):
//  1. this is ported to GLSL by me
//  2. tex sampler2D must be a linear sampler!
//------------------------------------------------------------------------------
// The following code is licensed under the MIT license: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
vec4 sampleTextureCatmullRom(in sampler2D tex, in vec2 uv, in vec2 texSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    vec2 samplePos = uv * texSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    vec2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    vec2 texPos0 = texPos1 - 1;
    vec2 texPos3 = texPos1 + 2;
    vec2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    vec4 result = vec4(0.0);
    result += textureLod(tex, vec2(texPos0.x, texPos0.y), 0.0) * w0.x * w0.y;
    result += textureLod(tex, vec2(texPos12.x, texPos0.y), 0.0) * w12.x * w0.y;
    result += textureLod(tex, vec2(texPos3.x, texPos0.y), 0.0) * w3.x * w0.y;

    result += textureLod(tex, vec2(texPos0.x, texPos12.y), 0.0) * w0.x * w12.y;
    result += textureLod(tex, vec2(texPos12.x, texPos12.y), 0.0) * w12.x * w12.y;
    result += textureLod(tex, vec2(texPos3.x, texPos12.y), 0.0) * w3.x * w12.y;

    result += textureLod(tex, vec2(texPos0.x, texPos3.y), 0.0) * w0.x * w3.y;
    result += textureLod(tex, vec2(texPos12.x, texPos3.y), 0.0) * w12.x * w3.y;
    result += textureLod(tex, vec2(texPos3.x, texPos3.y), 0.0) * w3.x * w3.y;

    return result;
}
// End of this specified license: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
////////////////////////////////////////////////////////////////////////////////

vec4 sampleTexture3dTetrahedralInterpolation(in sampler3D tex, in vec3 uv)
{
    //
    // Math/theory from
    //
    // "Real-Time Color Space Conversion for High Resolution Video"
    // Klaus Gaedke, Jörn Jachalsky
    // Technicolor Research & Innovation, Germany
    // klaus.gaedke@technicolor.com
    //
    // found at https://www.nvidia.com/content/GTC/posters/2010/V01-Real-Time-Color-Space-Conversion-for-High-Resolution-Video.pdf
    //

    ivec3 texSize = textureSize(tex, 0);
    vec3 texelCoord = saturate(uv) * vec3(texSize - ivec3(1));

    ivec3 texelCoord000 = ivec3(floor(texelCoord));
    ivec3 texelCoord111 = ivec3(ceil(texelCoord));
    vec4 texel000 = texelFetch(tex, texelCoord000, 0);
    vec4 texel111 = texelFetch(tex, texelCoord111, 0);

    vec3 f = fract(texelCoord);

    if (f.g >= f.b && f.b >= f.r) {

        vec4 texel010 = texelFetch(tex, texelCoord000 + ivec3(0, 1, 0), 0);
        vec4 texel011 = texelFetch(tex, texelCoord000 + ivec3(0, 1, 1), 0);
        return (1.0 - f.g) * texel000 + (f.g - f.b) * texel010 + (f.b - f.r) * texel011 + f.r * texel111;

    } else if (f.b > f.r && f.r > f.g) {

        vec4 texel001 = texelFetch(tex, texelCoord000 + ivec3(0, 0, 1), 0);
        vec4 texel101 = texelFetch(tex, texelCoord000 + ivec3(1, 0, 1), 0);
        return (1.0 - f.b) * texel000 + (f.b - f.r) * texel001 + (f.r - f.g) * texel101 + f.g * texel111;

    } else if (f.b > f.g && f.g >= f.r) {

        vec4 texel001 = texelFetch(tex, texelCoord000 + ivec3(0, 0, 1), 0);
        vec4 texel011 = texelFetch(tex, texelCoord000 + ivec3(0, 1, 1), 0);
        return (1.0 - f.b) * texel000 + (f.b - f.g) * texel001 + (f.g - f.r) * texel011 + f.r * texel111;

    } else if (f.r >= f.g && f.g > f.b) {

        vec4 texel100 = texelFetch(tex, texelCoord000 + ivec3(1, 0, 0), 0);
        vec4 texel110 = texelFetch(tex, texelCoord000 + ivec3(1, 1, 0), 0);
        return (1.0 - f.r) * texel000 + (f.r - f.g) * texel100 + (f.g - f.b) * texel110 + f.b * texel111;

    } else if (f.g > f.r && f.r >= f.b) {

        vec4 texel010 = texelFetch(tex, texelCoord000 + ivec3(0, 1, 0), 0);
        vec4 texel110 = texelFetch(tex, texelCoord000 + ivec3(1, 1, 0), 0);
        return (1.0 - f.g) * texel000 + (f.g - f.r) * texel010 + (f.r - f.b) * texel110 + f.b * texel111;

    } else if (f.r >= f.b && f.b >= f.g) {

        vec4 texel100 = texelFetch(tex, texelCoord000 + ivec3(1, 0, 0), 0);
        vec4 texel101 = texelFetch(tex, texelCoord000 + ivec3(1, 0, 1), 0);
        return (1.0 - f.r) * texel000 + (f.r - f.b) * texel100 + (f.b - f.g) * texel101 + f.g * texel111;

    }

    // Should be impossible to hit this
    return vec4(1.0, 0.0, 1.0, 1.0);
}

#endif // SAMPLING_GLSL

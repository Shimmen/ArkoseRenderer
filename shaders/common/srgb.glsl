#ifndef SRGB_GLSL
#define SRGB_GLSL

float sRGB_luminance(vec3 linear)
{
    return dot(linear, vec3(0.2126, 0.7152, 0.0722));
}

float sRGB_gammaEncodeScalar(float linear)
{
    return (linear < 0.0031308)
        ? 12.92 * linear
        : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

vec3 sRGB_gammaEncode(vec3 linear)
{
    return mix(12.92 * linear,
               1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
               greaterThanEqual(linear, vec3(0.0031308)));
}

float sRGB_gammaDecodeScalar(float encoded)
{
    return (encoded < 0.04045)
        ? encoded / 12.92
        : pow((encoded + 0.055) / 1.055, 2.4);
}

vec3 sRGB_gammaDecode(vec3 encoded)
{
    return mix(encoded / 12.92,
               pow((encoded + 0.055) / 1.055, vec3(2.4)),
               greaterThanEqual(encoded, vec3(0.04045)));
}

#endif // SRGB_GLSL

#ifndef ST2084_GLSL
#define ST2084_GLSL

// sRGB / Rec.709 => Rec.2020
const mat3 Rec2020_from_Rec709 = mat3(
    0.6274040,  0.0690970, 0.0163916,
    0.3292820, 0.9195400,  0.0880132,
    0.0433136, 0.0113612, 0.8955950
);

// See https://www.color.org/hdr/04-Timo_Kunkel.pdf
// and https://en.wikipedia.org/wiki/Perceptual_quantizer
vec3 perceptualQuantizerOETF(vec3 color)
{
    const float m1 = 2610.0 / 4096.0 / 4.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;

    vec3 Y = clamp(color, vec3(0.0), vec3(1.0));
    vec3 Ym1 = pow(Y, vec3(m1));

    return pow((c1 + c2 * Ym1) / (1.0 + c3 * Ym1), vec3(m2));
}

// NOTE: Assuming input color is in Rec.709 or sRGB space
vec3 toOutput_HDR10_ST2084(vec3 color, float paperWhiteLm)
{
    color = Rec2020_from_Rec709 * color;

    const float ST2084MaxLm = 10000.0;
    color *= paperWhiteLm / ST2084MaxLm;

    return perceptualQuantizerOETF(color);
}

#endif // ST2084_GLSL

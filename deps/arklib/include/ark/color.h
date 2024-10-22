/*
 * MIT License
 *
 * Copyright (c) 2020-2022 Simon Moos
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "matrix.h"
#include "vector.h"

namespace ark {

// NOTE: There is no one standard for this, but I will use this range
constexpr f32 visibleLightMinWavelength = 380.0f;
constexpr f32 visibleLightMaxWavelength = 780.0f;
constexpr f32 visibleLightWavelengthRangeLength = visibleLightMaxWavelength - visibleLightMinWavelength;
constexpr size_t visibleLightWavelengthRangeSteps = static_cast<size_t>(visibleLightWavelengthRangeLength) + 1;

namespace standardIlluminant {
    constexpr f32 D65 = 6504.0f;
}

namespace colorTemperature {
    // From or inspired by https://en.wikipedia.org/wiki/Color_temperature#Categorizing_different_lighting
    constexpr f32 candle = 1850.0f;
    constexpr f32 incandescentBulb = 2400.0f;
    constexpr f32 studioLight = 3200.0f;
    constexpr f32 fluorescentBulb = 5000.0f;
}

inline f32 blackBodyRadiation(f32 temperature, f32 wavelengthInNanometer)
{
    // From https://www.shadertoy.com/view/MstcD7 but it's also trivial to
    // reconstruct from Planck's Law: https://en.wikipedia.org/wiki/Planck%27s_law

    f32 h = 6.6e-34f; // Planck constant
    f32 kb = 1.4e-23f; // Boltzmann constant
    f32 c = 3e8f; // Speed of light

    f32 w = wavelengthInNanometer / 1e9f;
    const f32& t = temperature;

    f32 w5 = w * w * w * w * w;
    f32 o = 2.0f * h * (c * c) / (w5 * (std::exp((h * c) / (w * kb * t)) - 1.0f));

    return o;
}

namespace colorspace {

    namespace XYZ {

        // Assuming 1931 standard observer

        // xyz (bar) fits from Listing 1 of https://research.nvidia.com/publication/simple-analytic-approximations-cie-xyz-color-matching-functions

        inline f32 xBarFit(f32 wave)
        {
            f32 t1 = (wave - 442.0f) * ((wave < 442.0f) ? 0.0624f : 0.0374f);
            f32 t2 = (wave - 599.8f) * ((wave < 599.8f) ? 0.0264f : 0.0323f);
            f32 t3 = (wave - 501.1f) * ((wave < 501.1f) ? 0.0490f : 0.0382f);
            return 0.362f * std::exp(-0.5f * t1 * t1) + 1.056f * std::exp(-0.5f * t2 * t2) - 0.065f * std::exp(-0.5f * t3 * t3);
        }

        inline f32 yBarFit(f32 wave)
        {
            f32 t1 = (wave - 568.8f) * ((wave < 568.8f) ? 0.0213f : 0.0247f);
            f32 t2 = (wave - 530.9f) * ((wave < 530.9f) ? 0.0613f : 0.0322f);
            return 0.821f * std::exp(-0.5f * t1 * t1) + 0.286f * std::exp(-0.5f * t2 * t2);
        }

        inline f32 zBarFit(f32 wave)
        {
            f32 t1 = (wave - 437.0f) * ((wave < 437.0f) ? 0.0845f : 0.0278f);
            f32 t2 = (wave - 459.0f) * ((wave < 459.0f) ? 0.0385f : 0.0725f);
            return 1.217f * std::exp(-0.5f * t1 * t1) + 0.681f * std::exp(-0.5f * t2 * t2);
        }

        inline f32 photometricCurveFit(f32 wave)
        {
            return yBarFit(wave);
        }

        inline vec3 fromSingleWavelength(f32 power, f32 wavelength)
        {
            f32 X = xBarFit(wavelength);
            f32 Y = yBarFit(wavelength);
            f32 Z = zBarFit(wavelength);
            return power * vec3(X, Y, Z);
        }

        inline vec3 fromBlackBodyTemperature(f32 temperature, int numSteps = 100)
        {
            f32 stepWidth = visibleLightWavelengthRangeLength / static_cast<f32>(numSteps);

            vec3 XYZ = { 0, 0, 0 };
            for (int i = 0; i < numSteps; i++) {
                f32 mix = static_cast<f32>(i) / static_cast<f32>(numSteps - 1);
                f32 wavelength = lerp(visibleLightMinWavelength, visibleLightMaxWavelength, mix);
                f32 power = blackBodyRadiation(temperature, wavelength);
                XYZ += fromSingleWavelength(power, wavelength) * stepWidth;
            }

            return XYZ;
        }

        constexpr vec3 from_xyY(vec2 xy, f32 Y)
        {
            f32 scale = Y / xy.y;
            f32 X = scale * xy.x;
            f32 Z = scale * (1.0f - xy.x - xy.y);
            return { X, Y, Z };
        }

        constexpr vec2 to_xy(vec3 XYZ)
        {
            f32 sum = XYZ.x + XYZ.y + XYZ.z;
            f32 x = XYZ.x / sum;
            f32 y = XYZ.y / sum;
            return { x, y };
        }

        constexpr vec3 to_xyz(vec3 XYZ)
        {
            vec2 xy = to_xy(XYZ);
            f32 z = 1.0f - xy.x - xy.y;
            return { xy.x, xy.y, z };
        }

    } // namespace XYZ

    const mat3 XYZ_from_sRGB = mat3(
        { 0.4123865632529917f, 0.21263682167732384f, 0.019330620152483987f },
        { 0.35759149092062537f, 0.7151829818412507f, 0.11919716364020845f },
        { 0.18045049120356368f, 0.07218019648142547f, 0.9503725870054354f });

    const mat3 sRGB_from_XYZ = mat3(
        { 3.2410032329763587f, -0.9692242522025166f, 0.055639419851975444f },
        { -1.5373989694887855f, 1.875929983695176f, -0.20401120612390997f },
        { -0.4986158819963629f, 0.041554226340084724f, 1.0571489771875335f });

    namespace sRGB {

        // Primaries & white point from https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkColorSpaceKHR.html

        const vec2 primaries[3] = {
            vec2(0.64f, 0.33f),
            vec2(0.30f, 0.60f),
            vec2(0.15f, 0.06f)
        };

        const vec2 whitePoint = vec2(0.3127f, 0.3290f);
        const f32 whitePointIlluminant = standardIlluminant::D65;

        constexpr f32 luminance(const vec3& color)
        {
            constexpr vec3 Y = vec3(0.2126f, 0.7152f, 0.0722f);
            return dot(color, Y);
        }

        constexpr f32 gammaEncode(f32 linear)
        {
            // (i.e. convert from linear sRGB to gamma-encoded sRGB)
            return (linear < 0.0031308f)
                ? 12.92f * linear
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        }

        constexpr f32 gammaDecode(f32 encoded)
        {
            // (i.e. convert from gamma-encoded sRGB to linear sRGB)
            return (encoded < 0.04045f)
                ? encoded / 12.92f
                : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        constexpr vec3 gammaEncode(const vec3& linear)
        {
            return { gammaEncode(linear.x), gammaEncode(linear.y), gammaEncode(linear.z) };
        }

        constexpr vec3 gammaDecode(const vec3& encoded)
        {
            return { gammaDecode(encoded.x), gammaDecode(encoded.y), gammaDecode(encoded.z) };
        }

        inline vec3 fromBlackBodyTemperature(f32 temperature, int numSteps = 100)
        {
            vec3 XYZ = XYZ::fromBlackBodyTemperature(temperature, numSteps);
            vec3 sRGB = sRGB_from_XYZ * XYZ;
            return sRGB;
        }

    } // namespace sRGB

    const mat3 XYZ_from_Rec2020 = mat3(
        { 0.636953507f, 0.262698339f, 0.0280731358f },
        { 0.144619185f, 0.678008766f, 0.0280731358f },
        { 0.168855854f, 0.0592928953f, 1.06082723f });

    const mat3 Rec2020_from_XYZ = mat3(
        { 1.71666343f, -0.66667384f, 0.01764248f },
        { -0.35567332f, 1.61645574f, -0.04277698f },
        { -0.25336809f, 0.0157683f, 0.94224328f });

    namespace Rec2020 {

        // Primaries & white point from https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkColorSpaceKHR.html

        const vec2 primaries[3] = {
            vec2(0.708f, 0.292f),
            vec2(0.17f, 0.797f),
            vec2(0.131f, 0.046f)
        };

        const vec2 whitePoint = vec2(0.3127f, 0.3290f);
        const f32 whitePointIlluminant = standardIlluminant::D65;

        inline f32 encodePQfromLinear(f32 x, f32 maxNits)
        {
            // From https://www.khronos.org/registry/DataFormat/specs/1.3/dataformat.1.3.html#TRANSFER_PQ_IEOTF

            ARK_ASSERT(x >= 0.0f && x <= 1.0f);
            ARK_ASSERT(maxNits > 0.0f && maxNits <= 10'000.0f);

            // Scale it so that x=1 is equivalent to maxNits on a calibrated display. This is done
            // since 10'000 is not actually possible to achieve in practice on any modern displays.
            f32 L0 = x * maxNits / 10'000.0f;

            constexpr f32 c1 = 107.0f / 128.0f;
            constexpr f32 c2 = 2413.0f / 128.0f;
            constexpr f32 c3 = 2392.0f / 128.0f;
            constexpr f32 m1 = 1305.0f / 8192.0f;
            constexpr f32 m2 = 2523.0f / 32.0f;

            f32 L = std::pow(L0, m1);
            f32 V = std::pow((c1 + c2 * L) / (1.0f + c3 * L), m2);

            return V;
        }

        inline vec3 encodePQfromLinear(vec3 rgb, f32 maxNits = 1500.0f)
        {
            f32 r = encodePQfromLinear(rgb.x, maxNits);
            f32 g = encodePQfromLinear(rgb.y, maxNits);
            f32 b = encodePQfromLinear(rgb.z, maxNits);
            return { r, g, b };
        }

    } // namespace Rec2020

    namespace ACES {

        // This code is modified from 'Baking Lab' by MJP and David Neubelt (licensed under the MIT license):
        // https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl, who state
        // "The code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
        // credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)"

        // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
        const mat3 inputMatrix = mat3(
            { 0.59719f, 0.07600f, 0.02840f },
            { 0.35458f, 0.90834f, 0.13383f },
            { 0.04823f, 0.01566f, 0.83777f });

        // ODT_SAT => XYZ => D60_2_D65 => sRGB
        const mat3 outputMatrix = mat3(
            { 1.60475f, -0.10208f, -0.00327f },
            { -0.53108f, 1.10813f, -0.07276f },
            { -0.07367f, -0.00605f, 1.07602f });

        constexpr vec3 RRTAndODTFit(vec3 v)
        {
            vec3 a = v * (v + 0.0245786f) - 0.000090537f;
            vec3 b = v * (v * 0.983729f + 0.4329510f) + 0.238081f;
            return a / b;
        }

        constexpr vec3 referenceToneMap(vec3 color)
        {
            color = inputMatrix * color;
            color = RRTAndODTFit(color);
            color = outputMatrix * color;
            color = clamp(color, vec3(0.0f), vec3(1.0f));
            return color;
        }

    } // namespace ACES

    namespace HSV {

        inline vec3 toRGB(const vec3& HSV)
        {
            // From https://en.wikipedia.org/wiki/HSL_and_HSV#HSV_to_RGB

            // make sure H is in range [0, 360] degrees
            f32 h = HSV.x;
            h = std::fmod(h, 360.0f);
            h += 360.0;
            h = std::fmod(h, 360.0f);

            const f32& s = HSV.y;
            const f32& v = HSV.z;

            f32 c = v * s;
            f32 hPrim = h / 60.0f;
            f32 x = c * (1.0f - std::abs(std::fmod(hPrim, 2.0f) - 1.0f));
            f32 m = v - c;

            if (hPrim <= 1.0f)
                return { c + m, x + m, m };
            if (hPrim <= 2.0f)
                return { x + m, c + m, m };
            if (hPrim <= 3.0f)
                return { m, c + m, x + m };
            if (hPrim <= 4.0f)
                return { m, x + m, c + m };
            if (hPrim <= 5.0f)
                return { x + m, m, c + m };
            if (hPrim <= 6.0f)
                return { c + m, m, x + m };

            ARK_ASSERT(false);
            return { 0.0f, 0.0f, 0.0f };
        }

        constexpr vec3 fromRGB(vec3 RGB)
        {
            // From https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB

            RGB = clamp(RGB, vec3(0.0f), vec3(1.0f));
            const f32& r = RGB.x;
            const f32& g = RGB.y;
            const f32& b = RGB.z;

            f32 xMax = std::max(r, std::max(g, b));
            f32 xMin = std::min(r, std::min(g, b));
            f32 c = xMax - xMin;

            const f32& v = xMax;
            f32 s = (v == 0.0f)
                ? 0.0f
                : c / v;

            f32 h = 0.0f;
            if (c == 0.0f)
                h = 0.0f;
            else if (v == r)
                h = 60.0f * (g - b) / c;
            else if (v == g)
                h = 60.0f * (2.0f + (b - r) / c);
            else if (v == b)
                h = 60.0f * (4.0f + (r - g) / c);
            else
                ARK_ASSERT(false);

            return { h, s, v };
        }

    } // namespace HSV

} // namespace colorspace

// A 8-bit "storage" Color type, similarly to what you'd expect in a png or any other bitmap format
struct Color_sRGBA_U8 {
    u8 r { 0 };
    u8 g { 0 };
    u8 b { 0 };
    u8 a { 0 };
};

// A 16-bit "storage" Color type, similarly to what you'd expect in a high bit-depth png or exr
struct Color_sRGBA_U16 {
    u16 r { 0 };
    u16 g { 0 };
    u16 b { 0 };
    u16 a { 0 };
};

// An opinionated Color type
//  - unless otherwise noted, sRGB
//  - floating point (f32) storage
//  - normalized to [0, 1] range
//  - linear storage (no EOTFs)
//  - always pre-multiplied alpha
class Color {
public:
    f32 r() const { return m_r; }
    f32 g() const { return m_g; }
    f32 b() const { return m_b; }

    f32 alpha() const { return m_a; }
    f32 a() const { return alpha(); }

    static constexpr Color fromNonLinearSRGB(f32 r, f32 g, f32 b, f32 a)
    {
        ARK_ASSERT(a >= 0.0f);
        ARK_ASSERT(a <= 1.0f);

        if (isEffectivelyZero(a)) {
            return Color { 0.0f, 0.0f, 0.0f, 0.0f };
        } else {
            using namespace colorspace::sRGB;
            return Color { gammaDecode(r) * a, gammaDecode(g) * a, gammaDecode(b) * a, a };
        }
    }

    static constexpr Color fromNonLinearSRGB(vec3 rgb)
    {
        using namespace colorspace::sRGB;
        return Color { gammaDecode(rgb.x), gammaDecode(rgb.y), gammaDecode(rgb.z), 1.0f };
    }

    static constexpr Color fromNonLinearSRGB(f32 r, f32 g, f32 b)
    {
        using namespace colorspace::sRGB;
        return Color { gammaDecode(r), gammaDecode(g), gammaDecode(b), 1.0f };
    }

    // This is unsafe, in the sense that you're trusted to only input valid values!
    static constexpr Color fromFixedValuesUnsafe(f32 r, f32 g, f32 b, f32 a)
    {
        return Color { r, g, b, a };
    }

    constexpr vec4 asVec4() const
    {
        return vec4(r(), g(), b(), alpha());
    }

    constexpr vec3 asVec3() const
    {
        ARK_ASSERT(alpha() == 1.0f);
        return vec3(r(), g(), b());
    }

    constexpr f32* asFloatPointer()
    {
        return &m_r;
    }

    constexpr f32 const* asFloatPointer() const
    {
        return &m_r;
    }

    constexpr vec4 toNonLinearSRGB() const
    {
        using namespace colorspace::sRGB;
        f32 rNonLinear = gammaEncode(r());
        f32 gNonLinear = gammaEncode(g());
        f32 bNonLinear = gammaEncode(b());
        return vec4(rNonLinear, gNonLinear, bNonLinear, m_a);
    }

    constexpr vec4 toNonLinearSRGBUnPreMultiplied() const
    {
        if (isEffectivelyZero(alpha())) {
            return vec4(0.0f);
        } else {
            using namespace colorspace::sRGB;
            f32 rNonLinear = gammaEncode(r() / alpha());
            f32 gNonLinear = gammaEncode(g() / alpha());
            f32 bNonLinear = gammaEncode(b() / alpha());
            return vec4(rNonLinear, gNonLinear, bNonLinear, m_a);
        }
    }

    Color_sRGBA_U8 toStorageFormat_sRGBA_U8() const
    {
        Color_sRGBA_U8 rgba8;

        if (isEffectivelyZero(alpha())) { 
            rgba8.r = 0;
            rgba8.b = 0;
            rgba8.b = 0;
            rgba8.a = 0;
        } else {
            using namespace colorspace::sRGB;
            f32 rNonLinear = gammaEncode(r() / alpha());
            f32 gNonLinear = gammaEncode(g() / alpha());
            f32 bNonLinear = gammaEncode(b() / alpha());

            rgba8.r = static_cast<u8>(std::roundf(rNonLinear * 255.0f));
            rgba8.g = static_cast<u8>(std::roundf(gNonLinear * 255.0f));
            rgba8.b = static_cast<u8>(std::roundf(bNonLinear * 255.0f));
            rgba8.a = static_cast<u8>(std::roundf(alpha()    * 255.0f));
        }

        return rgba8;
    }

    Color_sRGBA_U16 toStorageFormat_sRGBA_U16() const
    {
        Color_sRGBA_U16 rgba16;

        if (isEffectivelyZero(alpha())) {
            rgba16.r = 0;
            rgba16.b = 0;
            rgba16.b = 0;
            rgba16.a = 0;
        } else {
            using namespace colorspace::sRGB;
            f32 rNonLinear = gammaEncode(r() / alpha());
            f32 gNonLinear = gammaEncode(g() / alpha());
            f32 bNonLinear = gammaEncode(b() / alpha());

            rgba16.r = static_cast<u16>(std::roundf(rNonLinear * 65'535.0f));
            rgba16.g = static_cast<u16>(std::roundf(gNonLinear * 65'535.0f));
            rgba16.b = static_cast<u16>(std::roundf(bNonLinear * 65'535.0f));
            rgba16.a = static_cast<u16>(std::roundf(alpha()    * 65'535.0f));
        }

        return rgba16;
    }

private:
    constexpr Color(f32 r, f32 g, f32 b, f32 a)
        : m_r { r }
        , m_g { g }
        , m_b { b }
        , m_a { a }
    {
        ARK_ASSERT(m_r >= 0.0f && m_r <= 1.0f);
        ARK_ASSERT(m_g >= 0.0f && m_g <= 1.0f);
        ARK_ASSERT(m_b >= 0.0f && m_b <= 1.0f);
        ARK_ASSERT(m_a >= 0.0f && m_a <= 1.0f);
    }

    f32 m_r { 0.0f };
    f32 m_g { 0.0f };
    f32 m_b { 0.0f };
    f32 m_a { 0.0f };
};

class Colors {
public:
    static constexpr Color transparent = Color::fromFixedValuesUnsafe(0.0f, 0.0f, 0.0f, 0.0f);

    static constexpr Color black       = Color::fromFixedValuesUnsafe(0.0f, 0.0f, 0.0f, 1.0f);
    static constexpr Color white       = Color::fromFixedValuesUnsafe(1.0f, 1.0f, 1.0f, 1.0f);

    static constexpr Color red         = Color::fromFixedValuesUnsafe(1.0f, 0.0f, 0.0f, 1.0f);
    static constexpr Color green       = Color::fromFixedValuesUnsafe(0.0f, 1.0f, 0.0f, 1.0f);
    static constexpr Color blue        = Color::fromFixedValuesUnsafe(0.0f, 0.0f, 1.0f, 1.0f);
};

} // namespace ark

#ifndef ARK_DONT_EXPOSE_COMMON_TYPES
using ark::Color;
using ark::Colors;
using ark::Color_sRGBA_U8;
using ark::Color_sRGBA_U16;
#endif

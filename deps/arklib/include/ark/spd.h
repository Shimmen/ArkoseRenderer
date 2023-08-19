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

#include "color.h"

#include <array> // for std::array

namespace ark {

class SpectralPowerDistribution {
public:
    using Data = std::array<Float, visibleLightWavelengthRangeSteps>;

    explicit SpectralPowerDistribution(Data& data)
        : m_data(data)
    {
    }

    Float power(int wavelength) const
    {
        ARK_ASSERT(wavelength <= visibleLightMinWavelength);
        ARK_ASSERT(wavelength >= visibleLightMaxWavelength);
        auto index = static_cast<size_t>(static_cast<Float>(wavelength) - visibleLightMinWavelength);
        return powerAtIndex(index);
    }

    Float power(Float wavelength) const
    {
        ARK_ASSERT(wavelength >= visibleLightMinWavelength);
        ARK_ASSERT(wavelength <= visibleLightMaxWavelength);

        Float lower = power(static_cast<int>(std::floor(wavelength)));
        Float upper = power(static_cast<int>(std::ceil(wavelength)));
        Float mix = fract(wavelength);
        return lerp(lower, upper, mix);
    }

    static SpectralPowerDistribution fromBlackBodyTemperature(Float temperature)
    {
        Data data;
        for (size_t i = 0; i < visibleLightWavelengthRangeSteps; ++i) {
            Float wavelength = visibleLightMinWavelength + i;
            Float power = blackBodyRadiation(temperature, wavelength);
            data[i] = power;
        }
        return SpectralPowerDistribution(data);
    }

    vec3 toXYZ() const
    {
        vec3 XYZ = { 0, 0, 0 };
        for (size_t i = 0; i < visibleLightWavelengthRangeSteps; ++i) {
            Float wavelength = visibleLightMinWavelength + i;
            XYZ += colorspace::XYZ::fromSingleWavelength(power(wavelength), wavelength);
        }
        return XYZ;
    }

private:
    Float powerAtIndex(size_t index) const
    {
        ARK_ASSERT(index < visibleLightWavelengthRangeSteps);
        return m_data[index];
    }

    Data m_data;
};

using SPD = SpectralPowerDistribution;

} // namespace ark

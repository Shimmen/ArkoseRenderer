#include "Halton.h"

vec2 halton::generateHaltonSample(int index, int baseX, int baseY)
{
    // Code is a modified version of the Halton sequence generation from
    // https://blog.demofox.org/2017/05/29/when-random-numbers-are-too-random-low-discrepancy-sequences/

    vec2 sample {};

    // x axis
    {
        float denominator = float(baseX);
        size_t n = index;
        while (n > 0) {
            size_t multiplier = n % baseX;
            sample.x += float(multiplier) / denominator;
            n = n / baseX;
            denominator *= baseX;
        }
    }

    // y axis
    {
        float denominator = float(baseY);
        size_t n = index;
        while (n > 0) {
            size_t multiplier = n % baseY;
            sample.y += float(multiplier) / denominator;
            n = n / baseY;
            denominator *= baseY;
        }
    }

    return sample;
}

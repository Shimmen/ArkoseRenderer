#include "Halton.h"

float halton::generateHaltonSample(int index, int base)
{
    // Code is a modified version of the Halton sequence generation from
    // https://blog.demofox.org/2017/05/29/when-random-numbers-are-too-random-low-discrepancy-sequences/

    float sample = 0.0f;

    float denominator = float(base);
    size_t n = index;
    while (n > 0) {
        size_t multiplier = n % base;
        sample += float(multiplier) / denominator;
        n = n / base;
        denominator *= base;
    }

    return sample;
}

#pragma once

constexpr size_t hashCombine(size_t a, size_t b)
{
    // TODO: Evaluate the quality of this..
    // Multiply with big primes and xor
    return (a * 137u) ^ (b * 383u);
}

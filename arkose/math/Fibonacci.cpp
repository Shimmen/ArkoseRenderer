#include "Fibonacci.h"

#include "core/Assert.h"

namespace geometry {

// Source: http://extremelearning.com.au/how-to-evenly-distribute-points-on-a-sphere-more-effectively-than-the-canonical-fibonacci-lattice/

vec2 fibonacciLattice(uint32_t i, uint32_t n)
{
    ARKOSE_ASSERT(i < n);

    static const float goldenRatio = (1.0f + std::sqrt(5.0f)) / 2.0f;

    float x = static_cast<float>(i) / goldenRatio;
    x -= int(x);

    float y = static_cast<float>(i) / static_cast<float>(n);

    return vec2(x, y);
}

vec2 fibonacciSpiral(uint32_t i, uint32_t n)
{
    vec2 latticePoint = fibonacciLattice(i, n);
    float angle = ark::TWO_PI * latticePoint.x;
    float radius = sqrt(latticePoint.y);
    return vec2(angle, radius);
}

vec3 sphericalFibonacci(uint32_t i, uint32_t n)
{
    vec2 latticePoint = fibonacciLattice(i, n);
    float theta = ark::TWO_PI * latticePoint.x;
    float phi = acos(2.0f * latticePoint.y - 1.0f);

    float sinPhi = sin(phi);
    return vec3(cos(theta) * sinPhi, sin(theta) * sinPhi, cos(phi));
}

}

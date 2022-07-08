#pragma once

#include "utility/Profiling.h"
#include <ark/core.h>
#include <ark/vector.h>

namespace geometry {

    // Calculate the Fibonacci lattice point (index i out of n) in a unit square [0, 1)^2
    vec2 fibonacciLattice(uint32_t i, uint32_t n);

    // Calculate the Fibonacci spiral point (index i out of n) in a unit circle (r=1).
    // Specified in polar coordinates (angle=[0, 2pi], radius=[0, 1].
    vec2 fibonacciSpiral(uint32_t i, uint32_t n);

    // Calculate the Fibonacci sphere / sphercial Fibonacci point (index i out of n) on the surface of a unit sphere (r=1)
    vec3 sphericalFibonacci(uint32_t i, uint32_t n);

}

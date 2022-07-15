#pragma once

#include <ark/vector.h>
#include <utility/Extent.h>

struct ProbeGrid {
    Extent3D gridDimensions;
    vec3 probeSpacing;
    vec3 offsetToFirst;

    int probeCount() const;

    ark::ivec3 probeIndexFromLinear(int index) const;
    vec3 probePositionForIndex(ark::ivec3) const;
};

#pragma once

#include <mooslib/vector.h>
#include <utility/Extent.h>

struct ProbeGrid {
    Extent3D gridDimensions;
    vec3 probeSpacing;
    vec3 offsetToFirst;

    int probeCount() const;

    moos::ivec3 probeIndexFromLinear(int index) const;
    vec3 probePositionForIndex(moos::ivec3) const;
};

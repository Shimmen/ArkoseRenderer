#pragma once

#include <moos/vector.h>
#include <utility/Extent.h>

// GPU shader data
#include "ProbeGridData.h"

struct ProbeGrid {
    Extent3D gridDimensions;
    vec3 probeSpacing;
    vec3 offsetToFirst;

    int probeCount() const;

    moos::ivec3 probeIndexFromLinear(int index) const;
    vec3 probePositionForIndex(moos::ivec3) const;

    ProbeGridData toProbeGridDataObject() const;
};

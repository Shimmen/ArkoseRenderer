#include "ProbeGrid.h"

int ProbeGrid::probeCount() const
{
    return gridDimensions.width()
        * gridDimensions.height()
        * gridDimensions.depth();
}

moos::ivec3 ProbeGrid::probeIndexFromLinear(int index) const
{
    auto findMSB = [](uint32_t val) -> int {
        ASSERT(val != 0);
        int index = 0;
        while ((val & 0x01) == 0 && index < 32) {
            val >>= 1;
            index += 1;
        }
        return index;
    };

    moos::ivec3 probeIndex;
    probeIndex.x = index & (gridDimensions.width() - 1);
    probeIndex.y = (index & ((gridDimensions.width() * gridDimensions.height()) - 1)) >> findMSB(gridDimensions.width());
    probeIndex.z = index >> findMSB(gridDimensions.width() * gridDimensions.height());

    return probeIndex;
}

vec3 ProbeGrid::probePositionForIndex(moos::ivec3 index) const
{
    vec3 floatIndex = { (float)index.x,
                        (float)index.y,
                        (float)index.z };
    return offsetToFirst + (floatIndex * probeSpacing);
}

ProbeGridData ProbeGrid::toProbeGridDataObject() const
{
    // TODO: This is some silly data shuffling. Could probably be unified to one, maybe?

    ProbeGridData data;
    data.gridDimensions = moos::ivec4(gridDimensions.width(), gridDimensions.height(), gridDimensions.depth(), 0);
    data.probeSpacing = vec4(probeSpacing, 0.0);
    data.offsetToFirst = vec4(offsetToFirst, 0.0);
    return data;
}

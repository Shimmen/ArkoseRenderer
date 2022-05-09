#include "ProbeGrid.h"

#include "core/Assert.h"

int ProbeGrid::probeCount() const
{
    return gridDimensions.width()
        * gridDimensions.height()
        * gridDimensions.depth();
}

moos::ivec3 ProbeGrid::probeIndexFromLinear(int index) const
{
    auto findMSB = [](uint32_t val) -> int {
        ARKOSE_ASSERT(val != 0);
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

#pragma once

#include "utility/EnumHelpers.h"

enum class ShaderStage {
    Vertex         = 0x001,
    Fragment       = 0x002,
    Compute        = 0x004,
    RTRayGen       = 0x008,
    RTMiss         = 0x010,
    RTClosestHit   = 0x020,
    RTAnyHit       = 0x040,
    RTIntersection = 0x080,
    Task           = 0x100,
    Mesh           = 0x200,

    AnyRasterize   = Vertex | Fragment | Task | Mesh,
    AnyRayTrace    = RTRayGen | RTMiss | RTClosestHit | RTAnyHit | RTIntersection,
    Any            = AnyRasterize | AnyRayTrace | Compute
};

ARKOSE_ENUM_CLASS_BIT_FLAGS(ShaderStage)

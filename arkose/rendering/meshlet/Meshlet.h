#pragma once

#include "core/Types.h"
#include <ark/vector.h>

// TODO: Probably move to shader-visible header!
struct Meshlet {
    u32 firstIndex;
    u32 triangleCount; // TODO: Make u8? Should never have more than 256 triangles! But we need the padding anyway..
    u32 _pad0;
    u32 _pad1;

    vec3 center;
    float radius;
};

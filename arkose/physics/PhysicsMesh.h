#pragma once

#include "PhysicsMaterial.h"
#include <ark/vector.h>
#include <vector>

struct PhysicsMesh {
    std::vector<vec3> positions;
    std::vector<uint32_t> indices;
    PhysicsMaterial material;
};

#pragma once

#include "core/Types.h"
#include "physics/PhysicsMaterial.h"
#include <ark/vector.h>
#include <vector>

struct PhysicsMesh {
    std::vector<vec3> positions;
    std::vector<u32> indices;

    //PhysicsMaterial material;

    template<class Archive>
    void serialize(Archive&, u32 version);
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "utility/EnumHelpers.h"
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

enum class PhysicsMeshVersion {
    Initial = 0,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    Count,
    LatestVersion = Count - 1
};

CEREAL_CLASS_VERSION(PhysicsMesh, toUnderlying(PhysicsMeshVersion::LatestVersion))

template<class Archive>
void PhysicsMesh::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(positions));
    archive(CEREAL_NVP(indices));
}

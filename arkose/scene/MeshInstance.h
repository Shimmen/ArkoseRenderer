#pragma once

#include "core/NonCopyable.h"
#include "rendering/StaticMesh.h"
#include "scene/Transform.h"

struct StaticMeshInstance {

    NON_COPYABLE(StaticMeshInstance)

    StaticMeshInstance(StaticMeshHandle inMesh, Transform inTransform)
        : mesh(inMesh)
        , transform(inTransform)
    {
    }

    // NOTE: If all meshes have the same handle value we know they can be instanced! :^)
    StaticMeshHandle mesh;

    // Optional; only needed if you want physics
    PhysicsInstanceHandle physicsInstance;

    Transform transform;
    std::string name;
};

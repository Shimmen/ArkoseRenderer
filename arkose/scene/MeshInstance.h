#pragma once

#include "rendering/StaticMesh.h"
#include "scene/Transform.h"

struct StaticMeshInstance {
    // NOTE: If all meshes have the same handle value we know they can be instanced! :^)
    StaticMeshHandle mesh;
    Transform transform;
};

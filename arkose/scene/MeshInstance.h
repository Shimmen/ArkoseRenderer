#pragma once

#include "rendering/StaticMesh.h"
#include "scene/Transform.h"

class StaticMeshInstance {
    // NOTE: If all meshes have the same handle value we know they can be instanced! :^)
    StaticMeshHandle m_mesh;
    Transform m_transform;
};

#include "StaticMesh.h"

////////////////////////////////////////////////////////////////////////////////
// Serialization

// We need to e.g. load in a Gltf file into this format. That could be somewhat slow though.
// Therefore this should also be doable at some cook time, in which we do the conversion and
// then serialize the ready-to-consume data. It would be neat to also e.g. zlib/zstd it.

// NOTE: This can be serialized during cook-time or so.
// When loading in PhysicsData, make a body from it and
// assign the handle to physicsShapeHandle of the StaticMesh.

/*
class PhysicsData {
    std::vector<uint8_t> m_data;
    PhysicsBackend::Type m_backend;
};

class StaticMeshWithPhysics {
    StaticMesh m_staticMesh;
    PhysicsData m_physicsData;
};
*/


void StaticMesh::writeToFile(/* some file stream */) const
{
    // Serialize mesh.
    // If we also have valid physics shapes, also write them
}
void StaticMesh::readFromFile(/* some file stream */) const
{
    // Serialize mesh.
    // If we also have valid physics shapes in the file, also read them
}

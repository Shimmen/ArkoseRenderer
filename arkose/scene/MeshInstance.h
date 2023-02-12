#pragma once

#include "core/NonCopyable.h"
#include "rendering/StaticMesh.h"
#include "scene/Transform.h"
#include "scene/editor/EditorObject.h"

struct StaticMeshInstance : public IEditorObject {
    NON_COPYABLE(StaticMeshInstance)

    StaticMeshInstance(StaticMeshHandle inMesh, Transform inTransform)
        : m_mesh(inMesh)
        , m_transform(inTransform)
    {
    }

    StaticMeshHandle mesh() const { return m_mesh; }
    PhysicsInstanceHandle physicsInstance() const { return m_physicsInstance; }

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    std::string name;

private:
    // NOTE: If all meshes have the same handle value we know they can be instanced! :^)
    StaticMeshHandle m_mesh;

    // Optional; only needed if you want physics
    PhysicsInstanceHandle m_physicsInstance;

    Transform m_transform;
};

#pragma once

#include "core/Handle.h"
#include "core/NonCopyable.h"
#include "rendering/Drawable.h"
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

    bool hasDrawableHandleForSegmentIndex(u32 segmentIdx) const;
    DrawableObjectHandle drawableHandleForSegmentIndex(u32 segmentIdx) const;
    std::vector<DrawableObjectHandle> const& drawableHandles() const { return m_drawableHandles; }

    void resetDrawableHandles();
    void setDrawableHandle(u32 segmentIndex, DrawableObjectHandle);

    std::string name;

private:
    // NOTE: If all meshes have the same handle value we know they can be instanced! :^)
    StaticMeshHandle m_mesh;

    // Optional; only needed if you want physics
    PhysicsInstanceHandle m_physicsInstance;

    // Handle for the drawables for the current underlying drawable object(s) (e.g. static mesh segments).
    // Can e.g. be used to get an index to the shader data for this segment.
    std::vector<DrawableObjectHandle> m_drawableHandles {};

    Transform m_transform;
};

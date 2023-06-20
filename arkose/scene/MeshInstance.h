#pragma once

#include "core/Handle.h"
#include "core/NonCopyable.h"
#include "rendering/Drawable.h"
#include "rendering/SkeletalMesh.h"
#include "rendering/Skeleton.h"
#include "rendering/StaticMesh.h"
#include "scene/Transform.h"
#include "scene/editor/EditorObject.h"

// TODO: Remove these instance types and instead just make them into components of an ECS (and move ITransformable to a separate component)

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

struct SkeletalMeshInstance : public IEditorObject {
    NON_COPYABLE(SkeletalMeshInstance)

    SkeletalMeshInstance(SkeletalMeshHandle inMesh, std::unique_ptr<Skeleton> skeleton, Transform inTransform)
        : m_mesh(inMesh)
        , m_skeleton(std::move(skeleton))
        , m_transform(inTransform)
    {
    }

    SkeletalMeshHandle mesh() const { return m_mesh; }
    //PhysicsInstanceHandle physicsInstance() const { return m_physicsInstance; }

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    Skeleton const& skeleton() const { return *m_skeleton; }
    Transform* findTransformForJoint(std::string_view jointName);

    bool hasDrawableHandleForSegmentIndex(u32 segmentIdx) const;
    DrawableObjectHandle drawableHandleForSegmentIndex(u32 segmentIdx) const;
    std::vector<DrawableObjectHandle> const& drawableHandles() const { return m_drawableHandles; }

    void resetDrawableHandles();
    void setDrawableHandle(u32 segmentIndex, DrawableObjectHandle);

    std::string name;

private:
    SkeletalMeshHandle m_mesh;

    // The skeleton / rig that is animated and drives the skinning of this skeletal mesh
    std::unique_ptr<Skeleton> m_skeleton;

    // Optional; only needed if you want physics
    //PhysicsInstanceHandle m_physicsInstance;

    // Handle for the drawables for the current underlying drawable object(s) (e.g. static mesh segments).
    // Can e.g. be used to get an index to the shader data for this segment.
    std::vector<DrawableObjectHandle> m_drawableHandles {};

    Transform m_transform;
};

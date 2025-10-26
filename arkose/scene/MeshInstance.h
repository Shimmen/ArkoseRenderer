#pragma once

#include <ark/handle.h>
#include <ark/copying.h>
#include "rendering/Drawable.h"
#include "rendering/SkeletalMesh.h"
#include "rendering/Skeleton.h"
#include "rendering/StaticMesh.h"
#include "scene/Transform.h"
#include "scene/editor/EditorObject.h"

// TODO: Remove these instance types and instead just make them into components of an ECS (and move ITransformable to a separate component)

struct StaticMeshInstance : public IEditorObject {
    ARK_NON_COPYABLE(StaticMeshInstance)

    StaticMeshInstance(StaticMeshHandle, Transform);
    ~StaticMeshInstance();

    StaticMeshHandle mesh() const { return m_mesh; }
    PhysicsInstanceHandle physicsInstance() const { return m_physicsInstance; }

    // IEditorObject interface
    bool shouldDrawGui() const override { return true; }
    void drawGui() override;

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    bool hasDrawableHandleForSegmentIndex(size_t segmentIdx) const;
    DrawableObjectHandle drawableHandleForSegmentIndex(size_t segmentIdx) const;
    std::vector<DrawableObjectHandle> const& drawableHandles() const { return m_drawableHandles; }

    void resetDrawableHandles();
    void setDrawableHandle(size_t segmentIndex, DrawableObjectHandle);

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

struct SkinningVertexMapping {
    // Allocation for the underlying mesh (segment), with all vertex data including skinning data.
    VertexAllocation underlyingMesh {};
    // Allocation for the target instance (still segment), where skinned vertices will be placed.
    VertexAllocation skinnedTarget {};
};

struct SkeletalMeshInstance : public IEditorObject {
    ARK_NON_COPYABLE(SkeletalMeshInstance)

    SkeletalMeshInstance(SkeletalMeshHandle, std::unique_ptr<Skeleton>, Transform);
    ~SkeletalMeshInstance();

    SkeletalMeshHandle mesh() const { return m_mesh; }
    //PhysicsInstanceHandle physicsInstance() const { return m_physicsInstance; }

    // IEditorObject interface
    bool shouldDrawGui() const override { return true; }
    void drawGui() override;

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    bool hasSkeleton() const { return m_skeleton != nullptr; }
    Skeleton const& skeleton() const { return *m_skeleton; }
    Skeleton& skeleton() { return *m_skeleton; }

    Transform* findTransformForJoint(std::string_view jointName);

    bool hasDrawableHandleForSegmentIndex(size_t segmentIdx) const;
    DrawableObjectHandle drawableHandleForSegmentIndex(size_t segmentIdx) const;
    std::vector<DrawableObjectHandle> const& drawableHandles() const { return m_drawableHandles; }

    void resetDrawableHandles();
    void setDrawableHandle(size_t segmentIndex, DrawableObjectHandle);

    bool hasSkinningVertexMappingForSegmentIndex(size_t segmentIdx) const;
    SkinningVertexMapping const& skinningVertexMappingForSegmentIndex(size_t segmentIdx) const;
    std::vector<SkinningVertexMapping> const& skinningVertexMappings() const { return m_skinningVertexMappings; }

    void resetSkinningVertexMappings();
    void setSkinningVertexMapping(size_t segmentIdx, SkinningVertexMapping);

    bool hasBlasForSegmentIndex(size_t segmentIdx) const;
    std::unique_ptr<BottomLevelAS> const& blasForSegmentIndex(size_t segmentIdx) const;
    std::vector<std::unique_ptr<BottomLevelAS>>& BLASes() { return m_blases; }

    void resetBLASes();
    void setBLAS(size_t segmentIdx, std::unique_ptr<BottomLevelAS>&&);

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

    // Skinning vertex mappings that map a vertex allocation in the underlying mesh to an allocation where
    // the animated vertices will be stored (one per segment, just as for drawable handles).
    std::vector<SkinningVertexMapping> m_skinningVertexMappings {};

    // Bottom-level acceleration structure for this instance (one per segment) (optional; only needed for ray tracing)
    std::vector<std::unique_ptr<BottomLevelAS>> m_blases {};

    Transform m_transform;
};

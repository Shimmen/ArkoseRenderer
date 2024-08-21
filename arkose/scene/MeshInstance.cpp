#include "MeshInstance.h"

////////////////////////////////////////////////////////////////////////////////
// StaticMeshInstance

StaticMeshInstance::StaticMeshInstance(StaticMeshHandle inMesh, Transform inTransform)
    : m_mesh(inMesh)
    , m_transform(inTransform)
{
}

StaticMeshInstance::~StaticMeshInstance() = default;

bool StaticMeshInstance::hasDrawableHandleForSegmentIndex(u32 segmentIdx) const
{
    return segmentIdx < m_drawableHandles.size();
}

DrawableObjectHandle StaticMeshInstance::drawableHandleForSegmentIndex(u32 segmentIdx) const
{
	return m_drawableHandles[segmentIdx];
}

void StaticMeshInstance::resetDrawableHandles()
{
    m_drawableHandles.clear();
}

void StaticMeshInstance::setDrawableHandle(u32 segmentIdx, DrawableObjectHandle drawableHandle)
{
    if (not hasDrawableHandleForSegmentIndex(segmentIdx)) {
        m_drawableHandles.resize(segmentIdx + 1, DrawableObjectHandle());
    }

    m_drawableHandles[segmentIdx] = drawableHandle;
}

////////////////////////////////////////////////////////////////////////////////
// SkeletalMeshInstance

SkeletalMeshInstance::SkeletalMeshInstance(SkeletalMeshHandle inMesh, std::unique_ptr<Skeleton> skeleton, Transform inTransform)
    : m_mesh(inMesh)
    , m_skeleton(std::move(skeleton))
    , m_transform(inTransform)
{
}

SkeletalMeshInstance::~SkeletalMeshInstance() = default;

Transform* SkeletalMeshInstance::findTransformForJoint(std::string_view jointName)
{
    return m_skeleton->findTransformForJoint(jointName);
}

bool SkeletalMeshInstance::hasDrawableHandleForSegmentIndex(size_t segmentIdx) const
{
    return segmentIdx < m_drawableHandles.size();
}

DrawableObjectHandle SkeletalMeshInstance::drawableHandleForSegmentIndex(size_t segmentIdx) const
{
    return m_drawableHandles[segmentIdx];
}

void SkeletalMeshInstance::resetDrawableHandles()
{
    m_drawableHandles.clear();
}

void SkeletalMeshInstance::setDrawableHandle(size_t segmentIdx, DrawableObjectHandle drawableHandle)
{
    if (not hasDrawableHandleForSegmentIndex(segmentIdx)) {
        m_drawableHandles.resize(segmentIdx + 1, DrawableObjectHandle());
    }

    m_drawableHandles[segmentIdx] = drawableHandle;
}

bool SkeletalMeshInstance::hasSkinningVertexMappingForSegmentIndex(size_t segmentIdx) const
{
    return segmentIdx < m_skinningVertexMappings.size();
}

SkinningVertexMapping const& SkeletalMeshInstance::skinningVertexMappingForSegmentIndex(size_t segmentIdx) const
{
    return m_skinningVertexMappings[segmentIdx];
}

void SkeletalMeshInstance::resetSkinningVertexMappings()
{
    m_skinningVertexMappings.clear();
}

void SkeletalMeshInstance::setSkinningVertexMapping(size_t segmentIdx, SkinningVertexMapping skinningVertexMapping)
{
    if (not hasSkinningVertexMappingForSegmentIndex(segmentIdx)) {
        m_skinningVertexMappings.resize(segmentIdx + 1);
    }

    m_skinningVertexMappings[segmentIdx] = skinningVertexMapping;
}

bool SkeletalMeshInstance::hasBlasForSegmentIndex(size_t segmentIdx) const
{
    return segmentIdx < m_blases.size();
}

std::unique_ptr<BottomLevelAS> const& SkeletalMeshInstance::blasForSegmentIndex(size_t segmentIdx) const
{
    return m_blases[segmentIdx];
}

void SkeletalMeshInstance::resetBLASes()
{
    m_blases.clear();
}

void SkeletalMeshInstance::setBLAS(size_t segmentIdx, std::unique_ptr<BottomLevelAS>&& blas)
{
    if (not hasBlasForSegmentIndex(segmentIdx)) {
        m_blases.resize(segmentIdx + 1);
    }

    m_blases[segmentIdx] = std::move(blas);
}

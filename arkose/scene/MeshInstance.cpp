#include "MeshInstance.h"

////////////////////////////////////////////////////////////////////////////////
// StaticMeshInstance

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

Transform* SkeletalMeshInstance::findTransformForJoint(std::string_view jointName)
{
    return m_skeleton->findTransformForJoint(jointName);
}

bool SkeletalMeshInstance::hasDrawableHandleForSegmentIndex(u32 segmentIdx) const
{
    return segmentIdx < m_drawableHandles.size();
}

DrawableObjectHandle SkeletalMeshInstance::drawableHandleForSegmentIndex(u32 segmentIdx) const
{
    return m_drawableHandles[segmentIdx];
}

void SkeletalMeshInstance::resetDrawableHandles()
{
    m_drawableHandles.clear();
}

void SkeletalMeshInstance::setDrawableHandle(u32 segmentIdx, DrawableObjectHandle drawableHandle)
{
    if (not hasDrawableHandleForSegmentIndex(segmentIdx)) {
        m_drawableHandles.resize(segmentIdx + 1, DrawableObjectHandle());
    }

    m_drawableHandles[segmentIdx] = drawableHandle;
}

bool SkeletalMeshInstance::hasSkinningVertexMappingForSegmentIndex(u32 segmentIdx) const
{
    return segmentIdx < m_skinningVertexMappings.size();
}

SkinningVertexMapping const& SkeletalMeshInstance::skinningVertexMappingForSegmentIndex(u32 segmentIdx) const
{
    return m_skinningVertexMappings[segmentIdx];
}

void SkeletalMeshInstance::resetSkinningVertexMappings()
{
    m_skinningVertexMappings.clear();
}

void SkeletalMeshInstance::setSkinningVertexMapping(u32 segmentIdx, SkinningVertexMapping skinningVertexMapping)
{
    if (not hasSkinningVertexMappingForSegmentIndex(segmentIdx)) {
        m_skinningVertexMappings.resize(segmentIdx + 1);
    }

    m_skinningVertexMappings[segmentIdx] = skinningVertexMapping;
}

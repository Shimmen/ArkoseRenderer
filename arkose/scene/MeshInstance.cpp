#include "MeshInstance.h"

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

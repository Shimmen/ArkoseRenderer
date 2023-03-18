#include "MeshInstance.h"

DrawableObjectHandle StaticMeshInstance::drawableHandleForSegmentIndex(u32 segmentIdx) const
{
	return m_drawableHandles[segmentIdx];
}

void StaticMeshInstance::resetDrawableHandles()
{
    m_drawableHandles.clear();
}

void StaticMeshInstance::setDrawableHandle(u32 segmentIndex, DrawableObjectHandle drawableHandle)
{
    if (segmentIndex >= m_drawableHandles.size()) {
        m_drawableHandles.resize(segmentIndex + 1, DrawableObjectHandle());
    }

    m_drawableHandles[segmentIndex] = drawableHandle;
}

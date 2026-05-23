#pragma once

#include <ark/handle.h>
#include <offsetAllocator/offsetAllocator.hpp>
#include "asset/HairAsset.h"
#include "core/Types.h"
#include "rendering/DrawKey.h"
#include "rendering/backend/util/DrawCall.h"

class HairAsset;

ARK_DEFINE_HANDLE_TYPE(HairHandle)

class HairMesh {
public:
    HairMesh(HairAsset const*);
    ~HairMesh();

    bool valid() const { return hairVertexAlloc.isValid() && indexAlloc.isValid() && strandAlloc.isValid(); }

    DrawCallDescription drawCallDescriptionAsLineStrip() const;
    DrawKey drawKey() const { return m_drawKey; }

    HairAsset const* hairAsset() const { return m_hairAsset; }

    OffsetAllocator::Allocation hairVertexAlloc {};
    OffsetAllocator::Allocation indexAlloc {};
    OffsetAllocator::Allocation strandAlloc {};

private:
    HairAsset const* m_hairAsset { nullptr };
    DrawKey m_drawKey {};
};

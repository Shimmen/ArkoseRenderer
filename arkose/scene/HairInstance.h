#pragma once

#include "rendering/Drawable.h"
#include "rendering/HairMesh.h"
#include "scene/Transform.h"
#include "scene/editor/EditorObject.h"
#include <ark/copying.h>
#include <ark/handle.h>

// TODO: Remove these instance types and instead just make them into components of an ECS (and move ITransformable to a separate component)

struct HairInstance : public IEditorObject {
    ARK_NON_COPYABLE(HairInstance)

    HairInstance(HairHandle, Transform);
    ~HairInstance();

    HairHandle hair() const { return m_hair; }

    void setDrawableHandle(DrawableObjectHandle handle) { m_drawableHandle = handle; }
    DrawableObjectHandle drawableHandle() const { return m_drawableHandle; }

    // IEditorObject interface
    bool shouldDrawGui() const override { return true; }
    void drawGui() override;

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    std::string name;

private:
    HairHandle m_hair;
    DrawableObjectHandle m_drawableHandle;
    Transform m_transform;
};

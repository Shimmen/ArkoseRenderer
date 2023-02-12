#pragma once

class Camera;

#include "rendering/Icon.h"
#include "scene/editor/EditorObject.h"
#include <ark/vector.h>
#include <string>

class EditorGizmo {
public:
    EditorGizmo(IconBillboard, IEditorObject&);

    bool isScreenPointInside(vec2 screenPoint) const;
    float distanceFromCamera() const;

    IconBillboard const& icon() const;
    Camera const& alignCamera() const;

    IEditorObject& editorObject() { return *m_editorObject; }
    IEditorObject const& editorObject() const { return *m_editorObject; }

    std::string debugName {};

private:
    IconBillboard m_icon;
    IEditorObject* m_editorObject;
};

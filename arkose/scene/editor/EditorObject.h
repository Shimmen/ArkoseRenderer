#pragma once

#include "scene/Transform.h"

class IEditorObject : public ITransformable {
public:
    virtual ~IEditorObject() {}

    virtual bool shouldDrawGui() const { return false; }
    virtual void drawGui() {};
};

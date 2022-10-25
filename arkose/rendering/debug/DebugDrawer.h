#pragma once

#include <ark/vector.h>
#include <vector>

class IDebugDrawer {
public:

    virtual void drawLine(vec3 p0, vec3 p1, vec3 color) = 0;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, vec3 color) = 0;

};

// This debug drawer does not actually draw anything itself but it will dispatch to registered drawers
class DebugDrawer : public IDebugDrawer {
public:

    static DebugDrawer& get();

    virtual void drawLine(vec3 p0, vec3 p1, vec3 color = vec3(1.0, 1.0, 1.0)) override;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, vec3 color = vec3(1.0, 1.0, 1.0)) override;

    void registerDebugDrawer(IDebugDrawer&);
    void unregisterDebugDrawer(IDebugDrawer&);

private:
    std::vector<IDebugDrawer*> m_debugDrawers {};

};

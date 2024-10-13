#pragma once

#include <ark/vector.h>
#include <string_view>
#include <vector>

class IconBillboard;

class IDebugDrawer {
public:

    virtual void drawLine(vec3 p0, vec3 p1, vec3 color) = 0;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, vec3 color) = 0;
    virtual void drawSphere(vec3 center, float radius, vec3 color) = 0;
    virtual void drawIcon(IconBillboard const&, vec3 tint) = 0;

};

// This debug drawer does not actually draw anything itself but it will dispatch to registered drawers
class DebugDrawer : public IDebugDrawer {
public:

    static DebugDrawer& get();

    virtual void drawLine(vec3 p0, vec3 p1, vec3 color = vec3(1.0f, 1.0f, 1.0f)) override;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, vec3 color = vec3(1.0f, 1.0f, 1.0f)) override;
    virtual void drawSphere(vec3 center, float radius, vec3 color = vec3(1.0f, 1.0f, 1.0f)) override;
    virtual void drawIcon(IconBillboard const&, vec3 tint = vec3(1.0f, 1.0f, 1.0f)) override;

    void registerDebugDrawer(IDebugDrawer&);
    void unregisterDebugDrawer(IDebugDrawer&);

private:
    std::vector<IDebugDrawer*> m_debugDrawers {};

    bool m_hasWarnedAboutNoDrawers { false };
    void validateDebugDrawersAreSetup(std::string_view context);

};

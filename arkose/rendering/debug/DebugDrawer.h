#pragma once

#include <ark/color.h>
#include <ark/matrix.h>
#include <ark/vector.h>
#include <string_view>
#include <vector>

class IconBillboard;
class Skeleton;

class IDebugDrawer {
public:

    virtual void drawLine(vec3 p0, vec3 p1, Color color) = 0;
    virtual void drawArrow(vec3 origin, vec3 direction, float length, Color color) = 0;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, Color color) = 0;
    virtual void drawSphere(vec3 center, float radius, Color color) = 0;
    virtual void drawIcon(IconBillboard const&, Color tint) = 0;
    virtual void drawSkeleton(Skeleton const&, mat4 rootTransform, Color color) = 0;

};

// This debug drawer does not actually draw anything itself but it will dispatch to registered drawers
class DebugDrawer : public IDebugDrawer {
public:

    static DebugDrawer& get();

    virtual void drawLine(vec3 p0, vec3 p1, Color color = Colors::white) override;
    virtual void drawArrow(vec3 origin, vec3 direction, float length, Color color = Colors::white) override;
    virtual void drawBox(vec3 minPoint, vec3 maxPoint, Color color = Colors::white) override;
    virtual void drawSphere(vec3 center, float radius, Color color = Colors::white) override;
    virtual void drawIcon(IconBillboard const&, Color tint = Colors::white) override;
    virtual void drawSkeleton(Skeleton const&, mat4 rootTransform, Color color = Colors::white) override;

    void registerDebugDrawer(IDebugDrawer&);
    void unregisterDebugDrawer(IDebugDrawer&);

private:
    std::vector<IDebugDrawer*> m_debugDrawers {};

    bool m_hasWarnedAboutNoDrawers { false };
    void validateDebugDrawersAreSetup(std::string_view context);

};

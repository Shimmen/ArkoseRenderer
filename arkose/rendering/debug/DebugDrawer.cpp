#include "DebugDrawer.h"

#include "rendering/Icon.h"

DebugDrawer& DebugDrawer::get()
{
    static DebugDrawer dispatchDrawer {};
    return dispatchDrawer;
}

void DebugDrawer::drawLine(vec3 p0, vec3 p1, Color color)
{
    validateDebugDrawersAreSetup("line");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawLine(p0, p1, color);
    }
}

void DebugDrawer::drawArrow(vec3 origin, vec3 direction, float length, Color color)
{
    validateDebugDrawersAreSetup("arrow");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawArrow(origin, direction, length, color);
    }
}

void DebugDrawer::drawBox(vec3 minPoint, vec3 maxPoint, Color color)
{
    validateDebugDrawersAreSetup("box");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawBox(minPoint, maxPoint, color);
    }
}

void DebugDrawer::drawSphere(vec3 center, float radius, Color color)
{
    validateDebugDrawersAreSetup("sphere");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawSphere(center, radius, color);
    }
}

void DebugDrawer::drawIcon(IconBillboard const& iconBillboard, Color tint)
{
    validateDebugDrawersAreSetup("icon");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawIcon(iconBillboard, tint);
    }
}

void DebugDrawer::drawSkeleton(Skeleton const& skeleton, mat4 rootTransform, Color color)
{
    validateDebugDrawersAreSetup("skeleton");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawSkeleton(skeleton, rootTransform, color);
    }
}

void DebugDrawer::registerDebugDrawer(IDebugDrawer& debugDrawer)
{
    m_debugDrawers.push_back(&debugDrawer);
}

void DebugDrawer::unregisterDebugDrawer(IDebugDrawer& debugDrawer)
{
    auto entry = std::find(m_debugDrawers.begin(), m_debugDrawers.end(), &debugDrawer);
    if (entry != m_debugDrawers.end()) {
        m_debugDrawers.erase(entry);
    }
}

void DebugDrawer::validateDebugDrawersAreSetup(std::string_view context)
{
    if (m_debugDrawers.size() == 0) {
        if (!m_hasWarnedAboutNoDrawers) {
            ARKOSE_LOG(Warning, "Attempting to draw {} but no debug drawers are hooked up so nothing will render!", context);
            m_hasWarnedAboutNoDrawers = true;
        }
    }
}

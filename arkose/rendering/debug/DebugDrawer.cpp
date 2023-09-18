#include "DebugDrawer.h"

#include "rendering/Icon.h"

DebugDrawer& DebugDrawer::get()
{
    static DebugDrawer dispatchDrawer {};
    return dispatchDrawer;
}

void DebugDrawer::drawLine(vec3 p0, vec3 p1, vec3 color)
{
    validateDebugDrawersAreSetup("line");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawLine(p0, p1, color);
    }
}

void DebugDrawer::drawBox(vec3 minPoint, vec3 maxPoint, vec3 color)
{
    validateDebugDrawersAreSetup("box");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawBox(minPoint, maxPoint, color);
    }
}

void DebugDrawer::drawSphere(vec3 center, float radius, vec3 color)
{
    validateDebugDrawersAreSetup("sphere");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawSphere(center, radius, color);
    }
}

void DebugDrawer::drawIcon(IconBillboard iconBillboard, vec3 tint)
{
    validateDebugDrawersAreSetup("icon");
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawIcon(iconBillboard, tint);
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
    if (!m_hasWarnedAboutNoDrawers) {
        ARKOSE_LOG(Warning, "Attempting to draw {} but no debug drawers are hooked up so nothing will render!", context);
        m_hasWarnedAboutNoDrawers = true;
    }
}

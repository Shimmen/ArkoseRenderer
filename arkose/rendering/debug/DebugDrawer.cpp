#include "DebugDrawer.h"

#include "rendering/Sprite.h"

DebugDrawer& DebugDrawer::get()
{
    static DebugDrawer dispatchDrawer {};
    return dispatchDrawer;
}

void DebugDrawer::drawLine(vec3 p0, vec3 p1, vec3 color)
{
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawLine(p0, p1, color);
    }
}

void DebugDrawer::drawBox(vec3 minPoint, vec3 maxPoint, vec3 color)
{
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawBox(minPoint, maxPoint, color);
    }
}

void DebugDrawer::drawSprite(Sprite sprite)
{
    for (IDebugDrawer* debugDrawer : m_debugDrawers) {
        debugDrawer->drawSprite(sprite);
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

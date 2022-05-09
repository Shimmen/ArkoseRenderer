#pragma once

#include "utility/Extent.h"

class AppState {
public:
    AppState() = default;
    AppState(const Extent2D& windowExtent, float deltaTime, float timeSinceStartup, uint32_t frameIndex, bool isRelativeFirstFrame)
        : m_frameIndex(frameIndex)
        , m_isRelativeFirstFrame(isRelativeFirstFrame)
        , m_windowExtent(windowExtent)
        , m_deltaTime(deltaTime)
        , m_timeSinceStartup(timeSinceStartup)

    {
    }

    uint32_t frameIndex() const { return m_frameIndex; }
    bool isFirstFrame() const { return m_frameIndex == 0; }
    bool isRelativeFirstFrame() const { return m_isRelativeFirstFrame; }
    const Extent2D& windowExtent() const { return m_windowExtent; }
    double deltaTime() const { return m_deltaTime; }
    double elapsedTime() const { return m_timeSinceStartup; }

    [[nodiscard]] AppState updateWindowExtent(Extent2D& newExtent)
    {
        AppState copy = *this;
        copy.m_windowExtent = newExtent;
        return copy;
    }

private:
    uint32_t m_frameIndex {};
    bool m_isRelativeFirstFrame {};
    Extent2D m_windowExtent {};

    float m_deltaTime {};
    float m_timeSinceStartup {};
};

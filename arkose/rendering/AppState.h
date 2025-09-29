#pragma once

#include "utility/Extent.h"

class AppState {
public:
    AppState() = default;
    AppState(float deltaTime, float timeSinceStartup, uint32_t frameIndex, bool isRelativeFirstFrame)
        : m_frameIndex(frameIndex)
        , m_isRelativeFirstFrame(isRelativeFirstFrame)
        , m_deltaTime(deltaTime)
        , m_timeSinceStartup(timeSinceStartup)

    {
    }

    uint32_t frameIndex() const { return m_frameIndex; }
    bool isFirstFrame() const { return m_frameIndex == 0; }
    bool isRelativeFirstFrame() const { return m_isRelativeFirstFrame; }
    float deltaTime() const { return m_deltaTime; }
    float elapsedTime() const { return m_timeSinceStartup; }

private:
    uint32_t m_frameIndex {};
    bool m_isRelativeFirstFrame {};

    float m_deltaTime {};
    float m_timeSinceStartup {};
};

#pragma once

#include "core/Assert.h"
#include <array>
#include <limits>

template<typename T, size_t RunningAvgWindowSizeT>
class AvgAccumulator {
public:
    static constexpr size_t RunningAvgWindowSize = RunningAvgWindowSizeT;

    AvgAccumulator() = default;

    void report(T value)
    {
        size_t index = m_numReported % RunningAvgWindowSize;
        m_samples[index] = value;
        m_totalAverage = ((m_totalAverage * m_numReported) + value) / (m_numReported + 1);
        m_numReported += 1;
    }

    T average() const
    {
        return m_totalAverage;
    }

    T runningAverage() const
    {
        if (m_numReported < RunningAvgWindowSize)
            return std::numeric_limits<T>::quiet_NaN();
        T sum = static_cast<T>(0.0);
        for (const T& sample : m_samples)
            sum += sample;
        return sum / static_cast<T>(RunningAvgWindowSize);
    }

    T valueAtSequentialIndex(size_t idx) const
    {
        ARKOSE_ASSERT(idx < RunningAvgWindowSize);

        if (m_numReported < 1)
            return static_cast<T>(0.0);

        int circularIdx = (m_numReported + idx) % RunningAvgWindowSize;
        return m_samples[circularIdx];
    }

private:
    T m_totalAverage = static_cast<T>(0.0);
    std::array<T, RunningAvgWindowSize> m_samples {};
    size_t m_numReported = 0u;
};

#pragma once

#include <array>

template<typename T, size_t RunningAvgWindowSize>
class AvgAccumulator {
public:
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
        T sum = static_cast<T>(0.0);
        for (const T& sample : m_samples)
            sum += sample;
        return sum / static_cast<T>(RunningAvgWindowSize);
    }

private:
    T m_totalAverage = static_cast<T>(0.0);
    std::array<T, RunningAvgWindowSize> m_samples {};
    size_t m_numReported = 0u;
};

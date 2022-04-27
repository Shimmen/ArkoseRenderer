#pragma once

#include "utility/AvgAccumulator.h"
#include <string>

class AvgElapsedTimer {
public:
    void reportCpuTime(double);
    double averageCpuTime() const;

    void reportGpuTime(double);
    double averageGpuTime() const;

    std::string createFormattedString() const;
    void plotTimes(float rangeMin, float rangeMax, float plotHeight) const;

private:
    using AvgAccumulatorType = AvgAccumulator<double, 60>;
    AvgAccumulatorType m_cpuAccumulator;
    AvgAccumulatorType m_gpuAccumulator;
};

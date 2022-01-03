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

private:
    AvgAccumulator<double, 60> m_cpuAccumulator;
    AvgAccumulator<double, 60> m_gpuAccumulator;
};

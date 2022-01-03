#include "AvgElapsedTimer.h"

#include <fmt/format.h>

void AvgElapsedTimer::reportCpuTime(double time)
{
    m_cpuAccumulator.report(time);
}

double AvgElapsedTimer::averageCpuTime() const
{
    return m_cpuAccumulator.runningAverage();
}

void AvgElapsedTimer::reportGpuTime(double time)
{
    m_gpuAccumulator.report(time);
}

double AvgElapsedTimer::averageGpuTime() const
{
    return m_gpuAccumulator.runningAverage();
}

std::string AvgElapsedTimer::createFormattedString() const
{
    double cpu = averageCpuTime();
    double gpu = averageGpuTime();
    std::string cpuString = isnan(cpu) ? "-" : fmt::format("{:.2f} ms", cpu * 1000.0);
    std::string gpuString = isnan(gpu) ? "-" : fmt::format("{:.2f} ms", gpu * 1000.0);
    return fmt::format("CPU: {} | GPU: {}", cpuString.c_str(), gpuString.c_str());
}

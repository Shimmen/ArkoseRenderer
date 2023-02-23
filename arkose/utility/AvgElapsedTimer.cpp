#include "AvgElapsedTimer.h"

#include <format>
#include <imgui.h>

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
    std::string cpuString = isnan(cpu) ? "-" : std::format("{:.2f} ms", cpu * 1000.0);
    std::string gpuString = isnan(gpu) ? "-" : std::format("{:.2f} ms", gpu * 1000.0);
    return std::format("CPU: {} | GPU: {}", cpuString.c_str(), gpuString.c_str());
}

void AvgElapsedTimer::plotTimes(float rangeMin, float rangeMax, float plotHeight) const
{
    auto avgAccumulatorValuesGetter = [](void* data, int idx) -> float {
        const auto& avgAccumulator = *reinterpret_cast<AvgAccumulatorType*>(data);
        return static_cast<float>(avgAccumulator.valueAtSequentialIndex(idx)) * 1000.0f;
    };

    int valuesCount = static_cast<int>(AvgAccumulatorType::RunningAvgWindowSize);
    ImVec2 plotSize { 0.5f * ImGui::GetContentRegionAvail().x, plotHeight };

    ImGui::PlotLines("", avgAccumulatorValuesGetter, (void*)&m_cpuAccumulator, valuesCount, 0, "CPU", rangeMin, rangeMax, plotSize);
    ImGui::SameLine();
    ImGui::PlotLines("", avgAccumulatorValuesGetter, (void*)&m_gpuAccumulator, valuesCount, 0, "GPU", rangeMin, rangeMax, plotSize);
}

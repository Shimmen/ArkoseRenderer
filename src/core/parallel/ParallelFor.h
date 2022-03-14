#pragma once

#include "TaskGraph.h"

#include <vector>

template<typename Function>
void ParallelFor(size_t count, Function&& body, bool singleThreaded = false)
{
    if (count == 0) {
        return;
    }

    if (count == 1) {
        body(0u);
        return;
    }

    // (For debugging purposes)
    if (singleThreaded) {
        for (size_t i = 0; i < count; ++i) {
            body(i);
        }
        return;
    }

    size_t workersToEmploy = TaskGraph::get().workerThreadCountExcludingSelf();
    size_t threadsToEmploy = workersToEmploy + 1; // including this thread

    size_t numTasks = std::min(count, threadsToEmploy);
    size_t numAsyncTasks = numTasks - 1;

    std::vector<TaskHandle> tasks {};
    tasks.reserve(numAsyncTasks);

    size_t startIdx = 0;
    size_t stopIdx = -1;

    const float tasksPerWorker = float(count) / float(numTasks);
    float tasksPerWorkerError = 0.0f;

    // Issue N-1 async tasks
    for (size_t taskIdx = 0; taskIdx < numAsyncTasks; ++taskIdx) {
        
        size_t tasksForThisWorker = size_t(tasksPerWorker);
        tasksPerWorkerError += (tasksPerWorker - tasksForThisWorker);
        if (tasksPerWorkerError > 1.0f) {
            tasksPerWorkerError -= 1.0f;
            tasksForThisWorker += 1;
        }

        stopIdx = startIdx + tasksForThisWorker;
        tasks.push_back(TaskGraph::get().enqueueTask([&, startIdx, stopIdx]() {
            for (size_t i = startIdx; i < stopIdx; ++i) {
                body(i);
            }
        }));

        startIdx = stopIdx;
    }

    // Do the remainder of the work on this thread
    for (size_t i = stopIdx; i < count; ++i) {
        body(i);
    }

    TaskGraph::get().waitFor(tasks);
}

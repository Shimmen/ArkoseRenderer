#pragma once

#include "core/Logging.h"
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

    if (count > 1000) {
        ARKOSE_LOG(Warning, "ParallelFor with large count ({}), consider using ParallelForBatched to reduce task enqueue overhead.", count);
    }

    if (singleThreaded || !TaskGraph::isInitialized()) {
        for (size_t i = 0; i < count; ++i) {
            body(i);
        }
        return;
    }

    TaskGraph& taskGraph = TaskGraph::get();
    Task& rootTask = Task::createEmpty();

    // TODO: Perhaps use a divide-and-conquer strategy so the calling thread doesn't have to do all task setup
    for (size_t idx = 0; idx < count; ++idx) {

        Task& task = Task::createWithParent(rootTask, [&body, idx]() {
            body(idx);
        });

        task.autoReleaseOnCompletion();
        taskGraph.scheduleTask(task);
    }

    taskGraph.scheduleTask(rootTask);
    taskGraph.waitForCompletion(rootTask);
    rootTask.release();
}

template<typename Function>
void ParallelForBatched(size_t count, size_t batchSize, Function&& body, bool singleThreaded = false)
{
    ARKOSE_ASSERT(batchSize > 0);

    if (count == 0) {
        return;
    }

    if (batchSize == 1) {
        return ParallelFor(count, std::move(body), singleThreaded);
    }

    if (count <= batchSize || singleThreaded || !TaskGraph::isInitialized()) {
        for (size_t idx = 0; idx < count; ++idx) {
            body(idx);
        }
        return;
    }

    size_t batchCount = ark::divideAndRoundUp(count, batchSize);
    ParallelFor(batchCount, [&](size_t batchIdx) {

        size_t firstIdx = batchIdx * batchSize;
        size_t lastIdx = std::min(firstIdx + batchSize, count);

        for (size_t idx = firstIdx; idx < lastIdx; ++idx) {
            body(idx);
        }

    });
}

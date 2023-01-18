#pragma once

#include "core/Assert.h"
#include "core/Types.h"
#include "core/parallel/Task.h"
#include <concurrentqueue.h>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>

// TaskGraph / job system implementation based on the one outline here:
// https://blog.molecular-matters.com/tag/job-system/

class TaskGraph final {
public:

    ~TaskGraph();

    static void initialize();
    static void shutdown();

    static TaskGraph& get();

    void scheduleTask(Task&);
    void waitForCompletion(Task&);

    size_t workerThreadCount() const;
    size_t workerThreadCountExcludingSelf() const;

    bool thisThreadIsWorker() const;

    bool isGraphIdle() const;
    void waitUntilGraphIsIdle() const;

    Task* getNextTask(std::thread::id thisThreadId = std::this_thread::get_id());

private:

    TaskGraph(uint32_t numWorkerThreads);
    TaskGraph(TaskGraph&) = delete;
    TaskGraph& operator=(TaskGraph&) = delete;

    using TaskQueue = moodycamel::ConcurrentQueue<Task*>;
    using PerThreadTaskList = std::vector<std::unique_ptr<TaskQueue>>;
    using ThreadTaskQueueLookupMap = std::unordered_map<std::thread::id, TaskQueue*>;

    static PerThreadTaskList s_taskQueueList;
    static ThreadTaskQueueLookupMap s_taskQueueLookup;
    static std::mutex s_taskQueueListMutex;
    static std::atomic_bool s_validated;

    static TaskQueue& createTaskQueueForThisThread();
    static TaskQueue& taskQueueForThisThread();
    static TaskQueue& taskQueueForThreadWithIndex(size_t);
    static void validateTaskQueueMap(size_t expectedCount);

    //

    class Worker {
    public:

        Worker(TaskGraph&, uint64_t workerId, std::string name);
        ~Worker();

        const std::string& name() const { return m_name; }
        const std::thread::id& threadId() const;

        void triggerShutdown();
        void waitUntilShutdown();

        uint64_t numWaitingTasks() const { return taskQueueForThisThread().size_approx(); }
        bool isIdle() const { return m_idle.load(); }

    private:

        TaskGraph* m_taskGraph { nullptr };

        std::string m_name;
        uint64_t m_workerId;
        std::thread::id m_threadId {};

        uint64_t m_nextSequentialTaskId { 1 };

        std::optional<std::thread> m_thread {};
        std::atomic<bool> m_alive { true };

        std::atomic<bool> m_idle { false };
        std::mutex m_idleMutex {};
        std::condition_variable m_idleCondition {};

        TaskGraph::TaskQueue* m_taskQueue {};
    };

    std::vector<std::unique_ptr<Worker>> m_workers {};

};

#pragma once

#include "core/Assert.h"
#include "core/Types.h"
#include "core/parallel/Task.h"
#include <concurrentqueue.h>
#include <condition_variable>
#include <functional>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

// TaskGraph / job system implementation based on the one outline here:
// https://blog.molecular-matters.com/tag/job-system/

enum class QueueType {
    Default,
    Background,
};

enum class WorkStrategy {
    Default,
    BackgroundOnly,
};

class TaskGraph final {
public:

    ~TaskGraph();

    static void initialize();
    static void shutdown();

    static bool isInitialized();

    static TaskGraph& get();

    void scheduleTask(Task&, QueueType = QueueType::Default);
    void waitForCompletion(Task&);

    size_t workerThreadCount() const;
    size_t workerThreadCountExcludingSelf() const;

    bool thisThreadIsWorker() const;

    bool isGraphIdle() const;
    void waitUntilGraphIsIdle() const;

    Task* getNextTask(QueueType);
    Task* getNextTaskForWorkStrategy(WorkStrategy);

private:

    TaskGraph();
    TaskGraph(TaskGraph&) = delete;
    TaskGraph& operator=(TaskGraph&) = delete;

    using TaskQueue = moodycamel::ConcurrentQueue<Task*>;

    struct TaskQueues {
        TaskQueues();
        ~TaskQueues();

        TaskQueue& queue(QueueType);

    private:
        TaskQueue defaultQueue;
        TaskQueue backgroundQueue;
    };

    using PerThreadTaskQueues = std::vector<std::unique_ptr<TaskQueues>>;
    using ThreadTaskQueueLookupMap = std::unordered_map<std::thread::id, TaskQueues*>;

    static PerThreadTaskQueues s_taskQueueList;
    static ThreadTaskQueueLookupMap s_taskQueueLookup;
    static std::mutex s_taskQueueListMutex;
    static std::atomic_bool s_validated;

    static TaskQueues& createTaskQueuesForThisThread();
    static TaskQueues& taskQueuesForThisThread();
    static void validateTaskQueueMap(size_t expectedCount);

    //

    class Worker {
    public:

        Worker(TaskGraph&, WorkStrategy, u64 workerId, std::string name);
        ~Worker();

        const std::string& name() const { return m_name; }
        const std::thread::id& threadId() const;

        void triggerShutdown();
        void waitUntilShutdown();

        u64 numWaitingTasks(QueueType) const;
        bool isIdle() const { return m_idle.load(); }

    private:

        TaskGraph* m_taskGraph { nullptr };

        WorkStrategy m_strategy;

        std::string m_name;
        u64 m_workerId;
        std::thread::id m_threadId {};

        std::optional<std::thread> m_thread {};
        std::atomic<bool> m_alive { true };

        std::atomic<bool> m_idle { false };
        std::mutex m_idleMutex {};
        std::condition_variable m_idleCondition {};

        TaskGraph::TaskQueues* m_taskQueues {};
    };

    std::vector<std::unique_ptr<Worker>> m_workers {};

};

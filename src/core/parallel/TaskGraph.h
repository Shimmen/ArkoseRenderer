#pragma once

#include "utility/util.h"
#include <condition_variable>
#include <functional>
#include <list>
#include <stdint.h>
#include <thread>
#include <vector>

struct TaskHandle {
    uint64_t workerId;
    uint64_t sequentialId;
};

enum class TaskStatus {
    Waiting,
    InProgress,
    Completed,
};

class TaskGraph final {
public:

    ~TaskGraph();

    static void initialize();
    static void shutdown();

    static TaskGraph& get();

    //

    size_t workerThreadCount() const;
    size_t workerThreadCountExcludingSelf() const;

    bool thisThreadIsWorker() const;

    using TaskFunction = std::function<void()>;

    template<typename TaskFunc>
    TaskHandle enqueueTask(TaskFunc&& taskFunc)
    {
        Worker* workerToHandleTask = findFirstFreeWorker();
        if (!workerToHandleTask)
            workerToHandleTask = &findLeastBusyWorker();
        ASSERT(workerToHandleTask);

        auto task = std::make_unique<Task>(taskFunc);
        TaskHandle handle = workerToHandleTask->enqueTaskForWorker(std::move(task));
        
        return handle;
    }

    // TODO: Functions for enqueuing with dependencies. But I don't think I need that quite yet...

    TaskStatus checkStatus(TaskHandle) const;
    bool checkAllCompleted(std::vector<TaskHandle>) const;

    void waitFor(TaskHandle) const;
    void waitFor(std::vector<TaskHandle>) const;

    bool isGraphIdle() const;
    void waitUntilGraphIsIdle() const;

private:

    TaskGraph(uint32_t numWorkerThreads);
    TaskGraph(TaskGraph&) = delete;
    TaskGraph& operator=(TaskGraph&) = delete;

    class Task {
    public:

        Task(TaskFunction&&);
        ~Task();

        void execute() const;

        void setSequentialId(uint64_t sequentialId) { m_sequentialId = sequentialId; }
        uint64_t sequentialId() const { return m_sequentialId; }

    private:
        uint64_t m_sequentialId {};
        TaskFunction m_function {};
        //std::vector<std::unique_ptr<Task>> m_waitingTasks {}; // todo!
    };

    class Worker {
    public:

        Worker(uint64_t workerId, std::string name);
        ~Worker();

        const std::string& name() const { return m_name; }
        const std::thread::id& threadId() const;

        void triggerShutdown();
        void waitUntilShutdown();

        TaskHandle enqueTaskForWorker(std::unique_ptr<Task>&&);

        uint64_t numWaitingTasks() const { return m_numWaitingTasks.load(); }
        bool isIdle() const { return m_idle.load(); }

        uint64_t lastStartedSequentialTaskId() const { return m_lastStartedSequentialTaskId; }
        uint64_t lastCompletedSequentialTaskId() const { return m_lastCompletedSequentialTaskId; }

    private:

        std::string m_name;
        uint64_t m_workerId;
        std::thread::id m_threadId {};

        uint64_t m_nextSequentialTaskId { 1 };

        std::optional<std::thread> m_thread {};
        std::atomic<bool> m_alive { true };

        std::mutex m_idleMutex {};
        std::condition_variable m_idleCondition {};

        std::mutex m_modification_mutex {};

        std::atomic<bool> m_idle { true };
        std::atomic<uint64_t> m_numWaitingTasks { 0 };
        std::vector<std::unique_ptr<Task>> m_waitingTasks {};

        std::atomic<uint64_t> m_lastStartedSequentialTaskId { 0 };
        std::atomic<uint64_t> m_lastCompletedSequentialTaskId { 0 };
    };

    Worker* findFirstFreeWorker();
    Worker& findLeastBusyWorker();

    Worker& findWorkerForTaskHandle(TaskHandle) const;

    std::vector<std::unique_ptr<Worker>> m_workers {};

};
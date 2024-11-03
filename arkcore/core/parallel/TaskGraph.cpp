#include "TaskGraph.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <ark/random.h>
#include <atomic>
#include <fmt/format.h>

#define SCOPED_PROFILE_ZONE_TASKGRAPH() SCOPED_PROFILE_ZONE_COLOR(0xaa33aa)

static std::unique_ptr<TaskGraph> g_taskGraphInstance { nullptr };

std::mutex TaskGraph::s_taskQueueListMutex {};
std::atomic_bool TaskGraph::s_validated { false };

TaskGraph::PerThreadTaskQueues TaskGraph::s_taskQueueList {};
TaskGraph::ThreadTaskQueueLookupMap TaskGraph::s_taskQueueLookup {};

void TaskGraph::initialize()
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    Task::initializeTasks();

    if (std::thread::hardware_concurrency() == 1) {
        ARKOSE_LOG(Fatal, "TaskGraph: this CPU only supports a single hardware thread, which is not compatible with this TaskGraph, exiting.");
    }

    ARKOSE_ASSERT(g_taskGraphInstance == nullptr);
    g_taskGraphInstance = std::unique_ptr<TaskGraph>(new TaskGraph());
}

void TaskGraph::shutdown()
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    g_taskGraphInstance.reset();
    Task::shutdownTasks();
}

TaskGraph& TaskGraph::get()
{
    ARKOSE_ASSERT(g_taskGraphInstance != nullptr);
    return *g_taskGraphInstance;
}

TaskGraph::TaskGraph()
{
    unsigned int hardwareConcurrency = std::thread::hardware_concurrency();

    u32 numDefaultWorkerThreads = std::min(hardwareConcurrency - 1, 10u);

    // These don't necessarily need to be on hardware threads
    u32 numBackgroundWorkerThreads = 2;

    // NOTE: The +1 is for the main thread queues, i.e. this current one which doesn't get an explicit worker
    const size_t numExpectedTaskQueues = (numDefaultWorkerThreads + numBackgroundWorkerThreads) + 1;

    createTaskQueuesForThisThread();

    u64 workerId = 1;

    for (size_t i = 0; i < numDefaultWorkerThreads; ++i) {
        std::string workerName = fmt::format("TaskGraphWorker{}", i + 1);
        m_workers.push_back(std::make_unique<Worker>(*this, WorkStrategy::Default, workerId, workerName));
        workerId += 1;
    }

    for (size_t i = 0; i < numBackgroundWorkerThreads; ++i) {
        std::string workerName = fmt::format("TaskGraphBackgroundWorker{}", i + 1);
        m_workers.push_back(std::make_unique<Worker>(*this, WorkStrategy::BackgroundOnly, workerId, workerName));
        workerId += 1;
    }

    // Ensure all workers have created their task queues before progressing!
    // TODO: This is easy to implement with std::latch, but for some reason Tracy doesn't work nicely with it
    // and the program will never exit, since some Tracy thread lives on. Very weird but the current method works.
    while (true) {
        {
            std::scoped_lock<std::mutex> lock { s_taskQueueListMutex };
            if (s_taskQueueList.size() == numExpectedTaskQueues) {
                break;
            }
        }
        std::this_thread::yield();
    }

    validateTaskQueueMap(numExpectedTaskQueues);
}

TaskGraph::~TaskGraph()
{
    for (auto& worker : m_workers) {
        worker->triggerShutdown();
    }

    for (auto& worker : m_workers) {
        worker->waitUntilShutdown();
    }

    {
        std::scoped_lock<std::mutex> lock { s_taskQueueListMutex };
        s_taskQueueList.clear();
    }
}

size_t TaskGraph::workerThreadCount() const
{
    return m_workers.size();
}

size_t TaskGraph::workerThreadCountExcludingSelf() const
{
    size_t count = workerThreadCount();
    if (thisThreadIsWorker())
        return count - 1;
    return count;
}

bool TaskGraph::thisThreadIsWorker() const
{
    auto callingThreadId = std::this_thread::get_id();
    for (const auto& worker : m_workers) {
        if (callingThreadId == worker->threadId()) {
            return true;
        }
    }
    return false;
}

void TaskGraph::scheduleTask(Task& task, QueueType queueType)
{
    // Always enqueue on own queue, let other workers steal from this
    TaskQueues& taskQueues = taskQueuesForThisThread();
    TaskQueue& taskQueue = taskQueues.queue(queueType);
    taskQueue.enqueue(&task);
}

void TaskGraph::waitForCompletion(Task& task)
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    while (!task.isCompleted()) {
        if (Task* otherTask = getNextTask(QueueType::Default)) {
            SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Execute task", 0xaa33aa);
            otherTask->execute();
        } else {
            std::this_thread::yield();
        }
    }
}

bool TaskGraph::isGraphIdle() const
{
    for (auto& worker : m_workers) {
        if (!worker->isIdle()) {
            return false;
        }
    }
    return true;
}

void TaskGraph::waitUntilGraphIsIdle() const
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    while (!isGraphIdle()) {
        std::this_thread::sleep_for(std::chrono::nanoseconds::min());
    }
}

TaskGraph::TaskQueues::TaskQueues()
    : defaultQueue(1024)
    , backgroundQueue(100)
{
}

TaskGraph::TaskQueues::~TaskQueues()
{
}

TaskGraph::TaskQueue& TaskGraph::TaskQueues::queue(QueueType queueType)
{
    switch (queueType) {
    case QueueType::Default:
        return defaultQueue;
    case QueueType::Background:
        return backgroundQueue;
    default:
        ASSERT_NOT_REACHED();
    }
}

TaskGraph::TaskQueues& TaskGraph::createTaskQueuesForThisThread()
{
    std::scoped_lock<std::mutex> lock { s_taskQueueListMutex };

    auto& taskQueues = s_taskQueueList.emplace_back(std::make_unique<TaskQueues>());

    std::thread::id threadId = std::this_thread::get_id();
    ARKOSE_ASSERT(!s_taskQueueLookup.contains(threadId));
    s_taskQueueLookup[threadId] = taskQueues.get();

    return *taskQueues;
}

TaskGraph::TaskQueues& TaskGraph::taskQueuesForThisThread()
{
    // NOTE: All threads must register at startup, before ever calling this function!
    // After that the task queue map is immutable so we make no attempt at guarding access to it

    // TODO: Could use thread-local storage for this
    std::thread::id threadId = std::this_thread::get_id();
    auto entry = s_taskQueueLookup.find(threadId);

    ARKOSE_ASSERT(entry != s_taskQueueLookup.end());
    ARKOSE_ASSERT(entry->second != nullptr);

    TaskQueues* taskQueues = entry->second;
    return *taskQueues;
}

void TaskGraph::validateTaskQueueMap(size_t expectedCount)
{
    std::scoped_lock<std::mutex> lock { s_taskQueueListMutex };
    ARKOSE_ASSERT(s_taskQueueList.size() == expectedCount);

    s_validated = true;
}

Task* TaskGraph::getNextTask(QueueType queueType)
{
    Task* nextTask = nullptr;

    // Try grabbing one from the local queue
    TaskQueues& localTaskQueues = taskQueuesForThisThread();
    if (localTaskQueues.queue(queueType).try_dequeue(nextTask)) {
        return nextTask;
    }

    // Try stealing one from another thread's queue
    // NOTE: For now the queue is short enough that we can just try all.. (including our own queue again)
    for (auto& stealTaskQueue : s_taskQueueList) {
        if (stealTaskQueue->queue(queueType).try_dequeue(nextTask)) {
            return nextTask;
        }
    }

    return nullptr;
}

Task* TaskGraph::getNextTaskForWorkStrategy(WorkStrategy strategy)
{
    switch (strategy) {
    case WorkStrategy::Default: {
        Task* task = getNextTask(QueueType::Default);
        if (!task) {
            task = getNextTask(QueueType::Background);
        }
        return task;
    }
    case WorkStrategy::BackgroundOnly:
        return getNextTask(QueueType::Background);
    default:
        ASSERT_NOT_REACHED();
    }
}

TaskGraph::Worker::Worker(TaskGraph& owningTaskGraph, WorkStrategy strategy, u64 workerId, std::string name)
    : m_taskGraph(&owningTaskGraph)
    , m_strategy(strategy)
    , m_name(std::move(name))
    , m_workerId(workerId)
{
    m_thread = std::thread([this]() {

        {
            SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Worker setup", 0xaa33aa);
            Profiling::setNameForActiveThread(m_name.c_str());

            m_threadId = std::this_thread::get_id();
            m_taskQueues = &TaskGraph::createTaskQueuesForThisThread();

            while (!s_validated) {
                std::this_thread::sleep_for(std::chrono::nanoseconds::min());
            }
        }

        while (m_alive) {

            if (Task* taskToExecute = m_taskGraph->getNextTaskForWorkStrategy(m_strategy)) {

                SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Execute task", 0xaa33aa);

                m_idle = false;
                taskToExecute->execute();

            } else {

                m_idle = true;
                std::this_thread::yield();

                // TODO: Implement proper idle mode when no task has been found for a while

                /*
                m_idle = true;

                std::unique_lock<std::mutex> idleLock { m_idleMutex };
                m_idleCondition.wait(idleLock, [&]() {

                    if (not m_alive) {
                        return true;
                    }

                    if (m_taskQueue->try_dequeue(taskToExecute)) {
                        return true;
                    }

                    return false;

                    // return !(m_alive && m_taskQueue->size_approx() == 0);
                });

                m_idle = false;
                */

            }
        }
    });
}

TaskGraph::Worker::~Worker()
{
    if (m_thread.has_value()) {
        triggerShutdown();
        waitUntilShutdown();
    }
}

const std::thread::id& TaskGraph::Worker::threadId() const
{
    // We must have at least started the thread at this point and assigned its thread id!
    ARKOSE_ASSERT(m_threadId != std::thread::id());

    return m_threadId;
}

void TaskGraph::Worker::triggerShutdown()
{
    m_alive = false;
    m_idleCondition.notify_all();
}

void TaskGraph::Worker::waitUntilShutdown()
{
    if (m_thread.has_value()) {
        m_thread->join();
        m_thread = {};
    }
}

u64 TaskGraph::Worker::numWaitingTasks(QueueType queueType) const
{
    return taskQueuesForThisThread().queue(queueType).size_approx();
}

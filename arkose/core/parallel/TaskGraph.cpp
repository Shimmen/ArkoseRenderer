#include "TaskGraph.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include <ark/random.h>
#include <atomic>
#include <format>

#define SCOPED_PROFILE_ZONE_TASKGRAPH() SCOPED_PROFILE_ZONE_COLOR(0xaa33aa)

static std::unique_ptr<TaskGraph> g_taskGraphInstance { nullptr };

std::mutex TaskGraph::s_taskQueueListMutex {};
std::atomic_bool TaskGraph::s_validated { false };

TaskGraph::PerThreadTaskList TaskGraph::s_taskQueueList {};
TaskGraph::ThreadTaskQueueLookupMap TaskGraph::s_taskQueueLookup {};

void TaskGraph::initialize()
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    Task::initializeTasks();

    unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
    if (hardwareConcurrency == 1) {
        ARKOSE_LOG(Fatal, "TaskGraph: this CPU only supports a single hardware thread, which is not compatible with this TaskGraph, exiting.");
    }
    uint32_t numWorkerThreads = std::min(hardwareConcurrency - 1, 10u);

    ARKOSE_ASSERT(g_taskGraphInstance == nullptr);
    g_taskGraphInstance = std::unique_ptr<TaskGraph>(new TaskGraph(numWorkerThreads));
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

TaskGraph::TaskGraph(uint32_t numWorkerThreads)
{
    const size_t numExpectedTaskQueues = numWorkerThreads + 1;

    createTaskQueueForThisThread();

    for (size_t i = 0; i < numWorkerThreads; ++i) {
        uint64_t workerId = i + 1;
        std::string workerName = std::format("TaskGraphWorker{}", workerId);
        m_workers.push_back(std::make_unique<Worker>(*this, workerId, workerName));
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

void TaskGraph::scheduleTask(Task& task)
{
    // Always enqueue on own queue, let other workers steal from this
    TaskQueue& taskQueue = taskQueueForThisThread();
    taskQueue.enqueue(&task);
}

void TaskGraph::waitForCompletion(Task& task)
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    while (not task.isCompleted()) {
        if (Task* task = getNextTask()) {
            SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Execute task", 0xaa33aa);
            task->execute();
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

TaskGraph::TaskQueue& TaskGraph::createTaskQueueForThisThread()
{
    std::scoped_lock<std::mutex> lock { s_taskQueueListMutex };

    auto& taskQueue = s_taskQueueList.emplace_back(std::make_unique<TaskQueue>(1024));

    std::thread::id threadId = std::this_thread::get_id();
    ARKOSE_ASSERT(not s_taskQueueLookup.contains(threadId));
    s_taskQueueLookup[threadId] = taskQueue.get();

    return *taskQueue;
}

TaskGraph::TaskQueue& TaskGraph::taskQueueForThisThread()
{
    // NOTE: All threads must register at startup, before ever calling this function!
    // After that the task queue map is immutable so we make no attempt at guarding access to it

    // TODO: Could use thread-local storage for this
    std::thread::id threadId = std::this_thread::get_id();
    auto entry = s_taskQueueLookup.find(threadId);

    ARKOSE_ASSERT(entry != s_taskQueueLookup.end());
    ARKOSE_ASSERT(entry->second != nullptr);

    TaskQueue* taskQueue = entry->second;
    return *taskQueue;
}

TaskGraph::TaskQueue& TaskGraph::taskQueueForThreadWithIndex(size_t idx)
{
    ARKOSE_ASSERT(idx < s_taskQueueList.size());
    ARKOSE_ASSERT(s_taskQueueList[idx] != nullptr);
    return *s_taskQueueList[idx];
}

void TaskGraph::validateTaskQueueMap(size_t expectedCount)
{
    std::scoped_lock<std::mutex> lock { s_taskQueueListMutex };
    ARKOSE_ASSERT(s_taskQueueList.size() == expectedCount);

    s_validated = true;
}

Task* TaskGraph::getNextTask(std::thread::id thisThreadId)
{
    Task* nextTask = nullptr;

    // Try grabbing one from the local queue
    TaskQueue& localTaskQueue = taskQueueForThisThread();
    if (localTaskQueue.try_dequeue(nextTask)) {
        return nextTask;
    }

    // Try stealing one from another thread's queue
    // NOTE: For now the queue is short enough that we can just try all.. (including our own queue again)
    for (auto& stealTaskQueue : s_taskQueueList) {
        if (stealTaskQueue->try_dequeue(nextTask)) {
            return nextTask;
        }
    }

    return nullptr;
}

TaskGraph::Worker::Worker(TaskGraph& owningTaskGraph, uint64_t workerId, std::string name)
    : m_taskGraph(&owningTaskGraph)
    , m_name(std::move(name))
    , m_workerId(workerId)
{
    m_thread = std::thread([this]() {

        {
            SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Worker setup", 0xaa33aa);
            Profiling::setNameForActiveThread(m_name.c_str());

            m_threadId = std::this_thread::get_id();
            m_taskQueue = &TaskGraph::createTaskQueueForThisThread();

            while (not s_validated) {
                std::this_thread::sleep_for(std::chrono::nanoseconds::min());
            }
        }

        while (m_alive) {

            if (Task* taskToExecute = m_taskGraph->getNextTask(m_threadId)) {

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

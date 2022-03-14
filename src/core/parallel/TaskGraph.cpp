#include "TaskGraph.h"

#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <fmt/format.h>

#define SCOPED_PROFILE_ZONE_TASKGRAPH() SCOPED_PROFILE_ZONE_COLOR(0xaa33aa)

static TaskGraph* g_taskGraphInstance = nullptr;

void TaskGraph::initialize()
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    ASSERT(g_taskGraphInstance == nullptr);

    // For now only the "main thread" are dedicated and active most of the time
    constexpr int numDedicatedThreads = 1;

    unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
    if (hardwareConcurrency == 1)
        LogErrorAndExit("TaskGraph: this CPU only supports a single hardware thread, which is not compatible with this TaskGraph, exiting.\n");
    uint32_t numWorkerThreads = hardwareConcurrency - 1;

    g_taskGraphInstance = new TaskGraph(numWorkerThreads);
}

void TaskGraph::shutdown()
{
    SCOPED_PROFILE_ZONE_TASKGRAPH();

    if (g_taskGraphInstance != nullptr) {
        delete g_taskGraphInstance;
        g_taskGraphInstance = nullptr;
    }
}

TaskGraph& TaskGraph::get()
{
    ASSERT(g_taskGraphInstance != nullptr);
    return *g_taskGraphInstance;
}

TaskGraph::TaskGraph(uint32_t numWorkerThreads)
{
    for (size_t i = 0; i < numWorkerThreads; ++i) {
        uint64_t workerId = i + 1;
        std::string workerName = fmt::format("TaskGraphWorker{}", workerId);
        m_workers.push_back(std::make_unique<Worker>(workerId, workerName));
    }
}

TaskGraph::~TaskGraph()
{
    for (auto& worker : m_workers) {
        worker->triggerShutdown();
    }

    for (auto& worker : m_workers) {
        worker->waitUntilShutdown();
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

TaskStatus TaskGraph::checkStatus(TaskHandle task) const
{
    const Worker& worker = findWorkerForTaskHandle(task);
    if (worker.lastCompletedSequentialTaskId() >= task.sequentialId) {
        return TaskStatus::Completed;
    } else if (worker.lastStartedSequentialTaskId() >= task.sequentialId) {
        return TaskStatus::InProgress;
    } else {
        return TaskStatus::Waiting;
    }
}

bool TaskGraph::checkAllCompleted(std::vector<TaskHandle> tasks) const
{
    for (const TaskHandle& task : tasks) {
        if (checkStatus(task) != TaskStatus::Completed) {
            return false;
        }
    }
    return true;
}

void TaskGraph::waitFor(TaskHandle task) const
{
    std::vector<TaskHandle> tasks { task };
    TaskGraph::waitFor(tasks);
}

void TaskGraph::waitFor(std::vector<TaskHandle> tasks) const
{
    SCOPED_PROFILE_ZONE_COLOR(0xaa33aa);

    while (!checkAllCompleted(tasks)) {
        std::this_thread::sleep_for(std::chrono::nanoseconds::min());
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
    SCOPED_PROFILE_ZONE_COLOR(0xaa33aa);

    while (!isGraphIdle()) {
        std::this_thread::sleep_for(std::chrono::nanoseconds::min());
    }
}

TaskGraph::Task::Task(TaskFunction&& taskFunction)
    : m_function(taskFunction)
{
}

TaskGraph::Task::~Task()
{
    //ASSERT(no remaining dependent tasks)
}

void TaskGraph::Task::execute() const
{
    if (m_function) {
        m_function();
    }
}

TaskGraph::Worker* TaskGraph::findFirstFreeWorker()
{
    for (const auto& worker : m_workers) {
        if (worker->numWaitingTasks() == 0) {
            return worker.get();
        }
    }

    return nullptr;
}

TaskGraph::Worker& TaskGraph::findLeastBusyWorker()
{
    uint64_t fewestWaitingTasks = std::numeric_limits<uint64_t>::max();
    Worker* leastBusyWorker = nullptr;

    for (size_t idx = 0; idx < m_workers.size(); ++idx) {
        auto& worker = m_workers[idx];
        uint64_t numWaitingTasks = worker->numWaitingTasks();
        if (numWaitingTasks < fewestWaitingTasks) {
            fewestWaitingTasks = numWaitingTasks;
            leastBusyWorker = worker.get();
        }
    }

    ASSERT(leastBusyWorker != nullptr);
    ASSERT(fewestWaitingTasks != std::numeric_limits<uint64_t>::max());

    return *leastBusyWorker;
}

TaskGraph::Worker& TaskGraph::findWorkerForTaskHandle(TaskHandle handle) const
{
    ASSERT(handle.workerId >= 1);
    uint64_t workerIdx = handle.workerId - 1;
    ASSERT(workerIdx < m_workers.size());
    return *m_workers[workerIdx];
}

TaskGraph::Worker::Worker(uint64_t workerId, std::string name)
    : m_name(std::move(name))
    , m_workerId(workerId)
{
    m_thread = std::thread([this]() {

        {
            SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Worker setup", 0xaa33aa);
            Profiling::setNameForActiveThread(m_name.c_str());

            m_threadId = std::this_thread::get_id();
        }

        while (m_alive) {


            std::unique_ptr<Task> taskToExecute = nullptr;

            if (m_numWaitingTasks > 0)
            {
                SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Grab task", 0xaa33aa);
                std::scoped_lock<std::mutex> mod_lock { m_modification_mutex };
                
                m_numWaitingTasks -= 1;
                taskToExecute = std::move(m_waitingTasks.front());
                m_waitingTasks.erase(m_waitingTasks.begin());

                ASSERT(taskToExecute != nullptr);
                ASSERT(m_numWaitingTasks == m_waitingTasks.size());
            }

            if (taskToExecute != nullptr)
            {
                SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Execute task", 0xaa33aa);

                uint64_t taskSequentialId = taskToExecute->sequentialId();
                ASSERT(taskSequentialId > m_lastStartedSequentialTaskId);
                m_lastStartedSequentialTaskId = taskSequentialId;

                taskToExecute->execute();

                ASSERT(taskSequentialId > m_lastCompletedSequentialTaskId);
                m_lastCompletedSequentialTaskId = taskSequentialId;

            } else {
                SCOPED_PROFILE_ZONE_NAME_AND_COLOR("Heartbeat", 0xaa33aa);
            }

            if (m_numWaitingTasks == 0) {
                m_idle = true;
                std::unique_lock<std::mutex> idle_lock { m_idleMutex };
                m_idleCondition.wait(idle_lock, [&]() { return !(m_alive && m_numWaitingTasks == 0); });
                m_idle = false;
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
    ASSERT(m_threadId != std::thread::id());

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

TaskHandle TaskGraph::Worker::enqueTaskForWorker(std::unique_ptr<Task>&& task)
{
    // TODO: Considering putting a time limit on this and if it doesn't acquire the lock in time return false and try some other worker.
    std::scoped_lock<std::mutex> lock(m_modification_mutex);

    uint64_t taskSequentialId = m_nextSequentialTaskId++;
    task->setSequentialId(taskSequentialId);

    TaskHandle handle = { .workerId = m_workerId,
                          .sequentialId = taskSequentialId++ };

    m_waitingTasks.push_back(std::move(task));
    uint64_t previousValue = m_numWaitingTasks.fetch_add(1);

    // It's possible this worker is sleeping/idle, make sure we then also notify it of its new state
    if (previousValue == 0) {
        m_idleCondition.notify_all();
    }

    return handle;
}

#include "Task.h"

#include "core/Assert.h"
#include "core/Logging.h"

#if defined(NDEBUG)
 #define WITH_LIFETIME_TRACKING 0
#else
 #define WITH_LIFETIME_TRACKING 1
#endif

namespace {
#if WITH_LIFETIME_TRACKING
static std::atomic_int64_t g_numAliveTasks { 0 };
#endif
}

Task& Task::create(TaskFunction&& taskFunction)
{
    // NOTE: See auto-release logic!
    return *new Task(std::move(taskFunction), nullptr);
}

Task& Task::createEmpty()
{
    // NOTE: See auto-release logic!
    return *new Task(TaskFunction(), nullptr);
}

Task& Task::createWithParent(Task& parentTask, TaskFunction&& taskFunction)
{
    // NOTE: See auto-release logic!
    return *new Task(std::move(taskFunction), &parentTask);
}

Task::Task(TaskFunction&& taskFunction, Task* parentTask)
    : m_function(std::move(taskFunction))
    , m_parentTask(parentTask)
{
#if WITH_LIFETIME_TRACKING
    g_numAliveTasks.fetch_add(1);
#endif

    if (parentTask) {
        parentTask->m_unfinishedTasks.fetch_add(1);
    }
}

Task::~Task()
{
#if WITH_LIFETIME_TRACKING
    g_numAliveTasks.fetch_sub(1);
#endif

    ARKOSE_ASSERT(isCompleted());
}

bool Task::isCompleted() const
{
    return m_unfinishedTasks.load(std::memory_order_seq_cst) == 0;
}

void Task::release()
{
    // NOTE: Yeah I don't love this, but the idea is to expose and pass around Tasks as references, so I don't want to expose
    // the memory management of it. I'd prefer it to seem more of an implementation detail to the whole task graph system.
    delete this;
}

void Task::autoReleaseOnCompletion()
{
    m_autoReleaseOnCompletion = true;
}

void Task::execute()
{
    if (m_function) {
        m_function();
    }

    finish();
}

void Task::finish()
{
    bool completed = m_unfinishedTasks.fetch_sub(1) == 1;
    if (completed && m_parentTask) {
        m_parentTask->finish();
    }

    if (m_autoReleaseOnCompletion) {
        release();
    }
}

void Task::initializeTasks()
{
#if WITH_LIFETIME_TRACKING
    g_numAliveTasks = 0;
#endif
}

void Task::shutdownTasks()
{
#if WITH_LIFETIME_TRACKING
    i64 count = g_numAliveTasks;
    if (count != 0) {
        ARKOSE_LOG_FATAL("The number of freed tasks does not equal the number of allocated ones. Current count: {}", count);
    }
#endif
}

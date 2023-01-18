#pragma once

#include "core/NonCopyable.h"
#include "core/Types.h"
#include <atomic>
#include <functional>

using TaskFunction = std::function<void()>;

class Task final {
public:
    static Task& create(TaskFunction&&);
    static Task& createEmpty();
    static Task& createWithParent(Task& parentTask, TaskFunction&&);

    NON_COPYABLE(Task)
    ~Task();

    bool isCompleted() const;

    void release();
    void autoReleaseOnCompletion();

private:
    Task(TaskFunction&&, Task* parentTask);

    friend class TaskGraph;

    void execute();
    void finish();

    static void initializeTasks();
    static void shutdownTasks();

    TaskFunction m_function {};

    Task* m_parentTask { nullptr };
    std::atomic_int32_t m_unfinishedTasks { 1 };
    std::atomic_bool m_autoReleaseOnCompletion { false };
};

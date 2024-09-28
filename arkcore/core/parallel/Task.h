#pragma once

#include <ark/copying.h>
#include "core/Types.h"
#include <atomic>
#include <functional>

using TaskFunction = std::function<void()>;

class Task {
public:
    static Task& create(TaskFunction&&);
    static Task& createEmpty();
    static Task& createWithParent(Task& parentTask, TaskFunction&&);

    ARK_NON_COPYABLE(Task)
    virtual ~Task();

    void executeSynchronous();

    bool isCompleted() const;

    void release();
    virtual void autoReleaseOnCompletion();

protected:
    Task(TaskFunction&&, Task* parentTask);

    friend class TaskGraph;

private:
    void execute();
    void finish();

    static void initializeTasks();
    static void shutdownTasks();

    TaskFunction m_function {};

    Task* m_parentTask { nullptr };
    std::atomic_int32_t m_unfinishedTasks { 1 };
    std::atomic_bool m_autoReleaseOnCompletion { false };
};

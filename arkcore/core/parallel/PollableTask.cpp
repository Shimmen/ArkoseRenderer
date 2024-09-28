#include "PollableTask.h"

PollableTask::PollableTask(TaskFunction&& taskFunction)
    : Task(std::move(taskFunction), nullptr)
{
}

void PollableTask::autoReleaseOnCompletion()
{
    ARKOSE_ERROR("PollableTask::autoReleaseOnCompletion() not allowed as the poller/owner of the task needs to track its lifetime");
}

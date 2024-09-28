#pragma once

#include "core/parallel/Task.h"

class PollableTask : public Task {
public:
    ARK_NON_COPYABLE(PollableTask)
    virtual ~PollableTask() { }

    virtual void autoReleaseOnCompletion() override;

    virtual float progress() const = 0;
    virtual std::string status() const { return ""; }

protected:
    PollableTask(TaskFunction&&);
};

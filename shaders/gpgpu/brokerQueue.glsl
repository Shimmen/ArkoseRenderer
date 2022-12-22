#ifndef BROKER_QUEUE_GLSL
#define BROKER_QUEUE_GLSL

#extension GL_KHR_shader_subgroup_ballot : require

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_atomic_int64 : require

////////////////////////////////////////////////////////////////////////////////
//
// The macro below will define a broker queue data structure and API for it.
//
// For the strict Broker Queue implementations we have the functions:
//  `<name>EnqueueStrict` and
//  `<name>DequeueStrict`,
// which will return true or false depending on the success of the operation will try multiple times and take the element as
// the first parameter, as a in or out variable respectively.
//
// There's also an API for accessing the queue as a Broker Work Distributor (BWD) using the functions:
//  `<name>Enqueue` and
//  `<name>Dequeue`,
// which will return true or false depending on the success of the operation (will only try once) and take the element as
// the first parameter, as a in or out variable respectively.
//
// We also expose the function `<name>ApproximateCount` which returns an approximation of how much space is left in this queue.
// It should not be used for anything more than getting a rough idea of what's going on inside the queue.
//
// NOTE:
//  1) The QUEUE_SIZE must be a power-of-two
//  2) MAX_THREADS must correspond to how many concurrent threads we are running at a max (e.g., global_size * local_size)
//
// INITIALIZATION:
//  -- All data must be initialized to zero before calling into any broker queue API
//
//
// The Broker Queue described in the paper
//  "The Broker Queue: A Fast, Linearizable FIFO Queue for Fine-Granular Work Distribution on the GPU"
//  https://doi.org/10.1145/3205289.3205291 | https://arbook.icg.tugraz.at/schmalstieg/Schmalstieg_353.pdf
//  by Bernhard Kerbl, Michael Kenzel, Joerg H. Mueller, Dieter Schmalstieg, Markus Steinberger.
// Implementation based on CUDA source code from the paper, available at
//  https://bitbucket.org/brokering/broker-queue/src/master/
//
////////////////////////////////////////////////////////////////////////////////

#define MAKE_BROKER_QUEUE(queueName, ElementType, QUEUE_SIZE, MAX_THREADS)                               \
                                                                                                         \
    /* layout(set = Y, binding = X) */ buffer coherent restrict queueName##Block {                       \
        uint64_t queueName##HeadTail64;                                                                  \
        int queueName##Count;                                                                            \
        int queueName##Padding0;                                                                         \
        uint queueName##Tickets[QUEUE_SIZE];                                                             \
        ElementType queueName##RingBuffer[QUEUE_SIZE];                                                   \
    };                                                                                                   \
                                                                                                         \
    void queueName##Backoff()                                                                            \
    {                                                                                                    \
        /* Encourage this thread to back off a bit, giving room for other threads */                     \
        memoryBarrier(); /* in CUDA: __threadfence(); */                                                 \
    }                                                                                                    \
                                                                                                         \
    uint queueName##Distance()                                                                           \
    {                                                                                                    \
        uint64_t headTail = queueName##HeadTail64; /* atomic read! */                                    \
        uint head = uint(headTail & uint64_t(0xFFFFFFFF));                                               \
        uint tail = uint(headTail >> 32);                                                                \
        return head - tail;                                                                              \
    }                                                                                                    \
                                                                                                         \
    void queueName##WaitForTicket(uint idx, uint ticketNumber)                                           \
    {                                                                                                    \
        while (queueName##Tickets[idx] != ticketNumber) {                                                \
            queueName##Backoff(); /* back off */                                                         \
        }                                                                                                \
    }                                                                                                    \
                                                                                                         \
    bool queueName##EnsureDequeue()                                                                      \
    {                                                                                                    \
        int num = queueName##Count; /* atomic load! */                                                   \
                                                                                                         \
        bool ensurance = false;                                                                          \
        while (!ensurance && num > 0) {                                                                  \
            ensurance = atomicAdd(queueName##Count, -1) > 0;                                             \
            if (!ensurance) {                                                                            \
                num = atomicAdd(queueName##Count, 1) + 1;                                                \
            }                                                                                            \
        }                                                                                                \
                                                                                                         \
        return ensurance;                                                                                \
    }                                                                                                    \
                                                                                                         \
    bool queueName##EnsureEnqueue()                                                                      \
    {                                                                                                    \
        int num = queueName##Count; /* atomic load! */                                                   \
                                                                                                         \
        bool ensurance = false;                                                                          \
        while (!ensurance && num < int(QUEUE_SIZE)) {                                                    \
            ensurance = atomicAdd(queueName##Count, 1) < int(QUEUE_SIZE);                                \
            if (!ensurance) {                                                                            \
                num = atomicAdd(queueName##Count, -1) - 1;                                               \
            }                                                                                            \
        }                                                                                                \
                                                                                                         \
        return ensurance;                                                                                \
    }                                                                                                    \
                                                                                                         \
    void queueName##ReadData(out ElementType data)                                                       \
    {                                                                                                    \
        /* increment head (i.e., most significant 32-bits) by 1 */                                       \
        uint64_t position64 = atomicAdd(queueName##HeadTail64, uint64_t(0xFFFFFFFF) + uint64_t(0x1));    \
                                                                                                         \
        uint position = uint(position64 >> 32);                                                          \
        uint idx = position % uint(QUEUE_SIZE);                                                          \
                                                                                                         \
        queueName##WaitForTicket(idx, 2 * (position / uint(QUEUE_SIZE)) + 1);                            \
        data = queueName##RingBuffer[idx];                                                               \
        memoryBarrier(); /* TODO: Is this barrier sufficient? */                                         \
        queueName##Tickets[idx] = 2 * ((position + uint(QUEUE_SIZE)) / uint(QUEUE_SIZE));                \
    }                                                                                                    \
                                                                                                         \
    void queueName##PutData(in ElementType data)                                                         \
    {                                                                                                    \
        /* increment tail (i.e., least significant 32-bits) by 1 */                                      \
        uint64_t position64 = atomicAdd(queueName##HeadTail64, 1);                                       \
                                                                                                         \
        uint position = uint(position64 & uint64_t(0xFFFFFFFF));                                         \
        uint idx = position % uint(QUEUE_SIZE);                                                          \
        uint ticketNum = 2 * (position / uint(QUEUE_SIZE));                                              \
                                                                                                         \
        queueName##WaitForTicket(idx, ticketNum);                                                        \
        queueName##RingBuffer[idx] = data;                                                               \
        memoryBarrier(); /* TODO: Is this barrier sufficient? */                                         \
        queueName##Tickets[idx] = ticketNum + 1;                                                         \
    }                                                                                                    \
                                                                                                         \
    bool queueName##EnqueueStrict(in ElementType data)                                                   \
    {                                                                                                    \
        bool writeData = true;                                                                           \
        while (writeData && !queueName##EnsureEnqueue()) {                                               \
            uint dist = queueName##Distance();                                                           \
            if (uint(QUEUE_SIZE) <= dist && dist < uint(QUEUE_SIZE) + uint(MAX_THREADS / 2)) {           \
                writeData = false;                                                                       \
            } else {                                                                                     \
                queueName##Backoff(); /* sleep a little */                                               \
            }                                                                                            \
        }                                                                                                \
                                                                                                         \
        if (writeData) {                                                                                 \
            queueName##PutData(data);                                                                    \
        }                                                                                                \
                                                                                                         \
        return writeData;                                                                                \
    }                                                                                                    \
                                                                                                         \
    bool queueName##DequeueStrict(out ElementType data)                                                  \
    {                                                                                                    \
        bool hasData = true;                                                                             \
        while (hasData && !queueName##EnsureDequeue()) {                                                 \
            uint dist = queueName##Distance();                                                           \
            if (uint(QUEUE_SIZE) + uint(MAX_THREADS / 2) <= dist - 1) {                                  \
                hasData = false;                                                                         \
            } else {                                                                                     \
                queueName##Backoff(); /* sleep a little */                                               \
            }                                                                                            \
        }                                                                                                \
                                                                                                         \
        if (hasData) {                                                                                   \
            queueName##ReadData(data);                                                                   \
        }                                                                                                \
                                                                                                         \
        return hasData;                                                                                  \
    }                                                                                                    \
                                                                                                         \
    /* With this function we effectively get Broker Work Distributor (BWD) behaviour,                    \
    /* meaning no strict guarantee that that the queue is FIFO, but in practice it should be close. */   \
    bool queueName##Enqueue(in ElementType data)                                                         \
    {                                                                                                    \
        bool writeData = queueName##EnsureEnqueue();                                                     \
        if (writeData) {                                                                                 \
            queueName##PutData(data);                                                                    \
        }                                                                                                \
        return writeData;                                                                                \
    }                                                                                                    \
                                                                                                         \
    /* With this function we effectively get Broker Work Distributor (BWD) behaviour,                    \
    /* meaning no strict guarantee that that the queue is FIFO, but in practice it should be close. */   \
    bool queueName##Dequeue(out ElementType data)                                                        \
    {                                                                                                    \
        bool hasData = queueName##EnsureDequeue();                                                       \
        if (hasData) {                                                                                   \
            queueName##ReadData(data);                                                                   \
        }                                                                                                \
        return hasData;                                                                                  \
    }                                                                                                    \
                                                                                                         \
    /* NOTE: This is an approximation, don't rely on this for any accurate readings! */                  \
    uint queueName##ApproximateCount()                                                                   \
    {                                                                                                    \
        return queueName##Count;                                                                         \
    }

////////////////////////////////////////////////////////////////////////////////

#endif // BROKER_QUEUE_GLSL

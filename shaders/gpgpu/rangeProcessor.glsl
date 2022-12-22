#ifndef RANGE_PROCESSOR_GLSL
#define RANGE_PROCESSOR_GLSL

#include <gpgpu/brokerQueue.glsl>

////////////////////////////////////////////////////////////////////////////////
//
// The macro below will define function `bool consume<name>RangeForProcessing(out uint index)` which will
// return true and a valid index to process for this lane/thread, or false if we could not consume a range.
//
// Note that these functions must NOT be called from a subgroupElect():ed subgroup, it must be called for
// all lanes/threads in a subgroup to work correctly
//
////////////////////////////////////////////////////////////////////////////////

#define MAKE_RANGE_PROCESSOR(processorName, queueName, selfEnqueueTestCallback, nextInLineEnqueueTestCallback, failureCounterVar)                   \
                                                                                                                                                    \
    bool consume##processorName##RangeForProcessing(out uint index, out uint metadata)                                                              \
    {                                                                                                                                               \
        index = 0u;                                                                                                                                 \
        metadata = 0u;                                                                                                                              \
                                                                                                                                                    \
        uvec4 ballot = subgroupBallot(true);                                                                                                        \
        uint consumeCount = subgroupBallotBitCount(ballot);                                                                                         \
        uint laneIdx = subgroupBallotExclusiveBitCount(ballot);                                                                                     \
                                                                                                                                                    \
        /* Grab the next range if possible */                                                                                                       \
        uvec4 range = uvec4(0u, 0u, 0u, 0u);                                                                                                        \
        if (subgroupElect()) {                                                                                                                      \
                                                                                                                                                    \
            /* See if we *could* enqueue into our own own queue (for sub-ranges) AND in the next queue (for later results of this operation). */    \
            /* If we consume this range now but can't fit its results then we've gotten into a unrecoverable state and so it must be avoided! */    \
            /* TODO: If we're full-ish and `selfEnqueueTestCallback` returns false, are we not deadlocked?! I think yes. We need to relax this! */  \
            if (/*!selfEnqueueTestCallback() || */!nextInLineEnqueueTestCallback()) {                                                               \
                return false;                                                                                                                       \
            }                                                                                                                                       \
                                                                                                                                                    \
            if (!queueName##Dequeue(range)) {                                                                                                       \
                return false;                                                                                                                       \
            }                                                                                                                                       \
        }                                                                                                                                           \
                                                                                                                                                    \
        range = subgroupBroadcastFirst(range);                                                                                                      \
        uint rangeCount = range.y - range.x;                                                                                                        \
                                                                                                                                                    \
        /* Early-out if we could not grab a range) */                                                                                               \
        if (rangeCount == 0) {                                                                                                                      \
            return false;                                                                                                                           \
        }                                                                                                                                           \
                                                                                                                                                    \
        if (subgroupElect()) {                                                                                                                      \
                                                                                                                                                    \
            /* Create a new sub-range if needed */                                                                                                  \
            if (rangeCount > consumeCount) {                                                                                                        \
                                                                                                                                                    \
                uvec4 newRange = uvec4(range.x + consumeCount, range.y, range.z, range.w);                                                          \
                if (!queueName##Enqueue(newRange)) {                                                                                                \
                    /* NOTE: We can't gracefully handle this fail, this is why we check shouldAttemptEnqueue() before we even attemtp */            \
                    /*       any type of dequeuing, since we know it can lead to new sub-ranges being added. While we can't handle it */            \
                    /*       gracefully, we can at least make the application (CPU) aware of it and trigger an abort or similar.      */            \
                    atomicAdd(failureCounterVar, 1);                                                                                                \
                }                                                                                                                                   \
            }                                                                                                                                       \
                                                                                                                                                    \
            /* Assign the first index of the range to the elected/first lane */                                                                     \
            index = range.x;                                                                                                                        \
        }                                                                                                                                           \
                                                                                                                                                    \
        /* Assign consecutive indices of the range to consecutive active lanes */                                                                   \
        index = subgroupBroadcastFirst(index) + laneIdx;                                                                                            \
                                                                                                                                                    \
        /* Return metadata (range.z) for use */                                                                                                     \
        metadata = range.z;                                                                                                                         \
                                                                                                                                                    \
        /* The out index is only valid if we're within the range */                                                                                 \
        return laneIdx < rangeCount;                                                                                                                \
    }                                                                                                                                               \

////////////////////////////////////////////////////////////////////////////////

#endif // RANGE_PROCESSOR_GLSL

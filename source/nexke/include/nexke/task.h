/*
    task.h - contains multitasking interface
    Copyright 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef _TASK_H
#define _TASK_H

#include <nexke/cpu.h>
#include <nexke/list.h>
#include <nexke/types.h>
#include <stdbool.h>

// Thread entry type
typedef void (*NkThreadEntry) (void*);

// Wait obejct
// Defines an object this thread is waiting on, and is used to create priority chains
typedef struct _waitobj
{
    NkLink_t link;
    NkThread_t* owner;     // Owner of this object
    NkThread_t* waiter;    // Waiter
    int type;              // Type of object being waited on
    ktime_t timeout;       // Timeout of this object
    void* obj;             // Pointer to object being waited on
} TskWaitObj_t;

#define TSK_THREAD_MAX_WAIT 4

#define TSK_WAITOBJ_TIMER     0
#define TSK_WAITOBJ_MSG       1
#define TSK_WAITOBJ_SEMAPHORE 2
#define TSK_WAITOBJ_CONDITION 3
#define TSK_WAITOBJ_MUTEX     4

// Thread structure
typedef struct _thread
{
    NkLink_t link;    // Link in ready queue / wait lists
                      // At head so we don't have to use LINK_CONTAINER
    // Thread identity info
    id_t tid;            // Thread ID
    const char* name;    // Name of thread
    int priority;        // Priority of this thread
    int state;           // State of this thread
    // Quantum info
    int quantaLeft;    // Quantum ticks left
    int quantum;       // Quantum assigned to thread
    // CPU specific thread info
    CpuContext_t* context;    // Context of this thread
    CpuThread_t cpuThread;    // More CPU info
    // Time info
    ktime_t lastSchedule;    // Last time thread was scheduled
    ktime_t runTime;         // Time thread has run for
    // Entry point
    NkThreadEntry entry;
    void* arg;
    // Wait info
    TskWaitObj_t wait;            // Object we are waiting on
    TskWaitObj_t timer;           // Timer we are waiting on
    volatile int waitAsserted;    // Wheter a wait is currently asserted
    // Thread flags
    bool preempted;            // Wheter this thread has been preempted
    NkTimeEvent_t* timeout;    // Wait queue timeout
    bool timeoutPending;       // Wheter a timeout is pending
} NkThread_t;

// Helpers for wait assertion
#define TSK_SET_THREAD_ASSERT(thread, val) \
    (__atomic_store_n (&(thread)->waitAsserted, (val), __ATOMIC_RELEASE))
#define TSK_CHECK_THREAD_ASSERT(thread)                                    \
    int __val = 0;                                                         \
    do                                                                     \
    {                                                                      \
        __atomic_load (&(thread)->waitAsserted, &__val, __ATOMIC_ACQUIRE); \
    } while (__val);

// Thread states
#define TSK_THREAD_READY   0
#define TSK_THREAD_RUNNING 1
#define TSK_THREAD_WAITING 2
#define TSK_THREAD_CREATED 3

// Maybe this should be bigger
#define NEXKE_MAX_THREAD 8192

// Initializes task system
void TskInitSys();

// Initializes scheduler
void TskInitSched();

// Creates a new thread object
NkThread_t* TskCreateThread (NkThreadEntry entry, void* arg, const char* name);

// Sets the initial thread to execute in the system
void __attribute__ ((noreturn)) TskSetInitialThread (NkThread_t* thread);

// IPL unsafe functions
// This is the main scheduler interface

// Destroys a thread object
// NOTE: always call terminate over this function
void TskDestroyThread (NkThread_t* thread);

// Sets the current thread
void TskSetCurrentThread (NkThread_t* thread);

// Admits thread to ready queue
void TskReadyThread (NkThread_t* thread);

// Runs the main scheduler
void TskSchedule();

// Blocks current thread
void TskBlockThread();

// Asserts and sets up a wait
// IPL must be raised and object must be locked
TskWaitObj_t* TskAssertWait (NkThread_t* objOwner, ktime_t timeout, void* obj, int type);

// Cleans up a wait object
bool TskFinishWait (TskWaitObj_t* waitObj);

// Clears a wait on a wait object
// If timeout already expired, returns false
bool TskClearWait (TskWaitObj_t* waitObj);

// IPL safe functions

// Enables preemption (only used by TskEnablePreempt)
void TskEnablePreemptUnsafe();

// Disables preemption
static inline void TskDisablePreempt()
{
    ++CpuGetCcb()->preemptDisable;
}

static inline void TskEnablePreempt()
{
    NkCcb_t* ccb = CpuGetCcb();
    --ccb->preemptDisable;
    if (ccb->preemptDisable == 0)
        TskEnablePreemptUnsafe();
}

// Gets current thread
NkThread_t* TskGetCurrentThread();

// Yields from current thread
// Safe wrapper over TskSchedule
void TskYield();

// Quantum stuff

// Time slicer operating delta (in ns)
#define TSK_TIMESLICE_DELTA 10000000

// Default time slice length (in time slicer ticks)
#define TSK_TIMESLICE_LEN 6    // equals 60 ms

// Wait queue
typedef struct _wq
{
    NkList_t waiters;    // Waiters waiting on this queue
    spinlock_t lock;     // Queue lock
    int queueObject;     // Type of object queue is working under
    bool done;           // If this queue is done
} TskWaitQueue_t;

// Initializes a wait queue
void TskInitWaitQueue (TskWaitQueue_t* queue, int object);

// Closes a wait queue and broadcasts the closing
errno_t TskCloseWaitQueue (TskWaitQueue_t* queue, int flags);

// Broadcasts wakeup to all threads on the queue
errno_t TskBroadcastWaitQueue (TskWaitQueue_t* queue, int flags);

// Wakes one thread on the queue
errno_t TskWakeWaitQueue (TskWaitQueue_t* queue, int flags);

// Waits on the specified wait queue
errno_t TskWaitQueue (TskWaitQueue_t* queue);

// Waits with timeout
errno_t TskWaitQueueTimeout (TskWaitQueue_t* queue, ktime_t timeout);

// Waits with flags
errno_t TskWaitQueueFlags (TskWaitQueue_t* queue, int flags, ktime_t timeout);

// Asserts that we are going to wait
// Returns old IPL pre-assert
ipl_t TskAssertWaitQueue (TskWaitQueue_t* queue);

// Deasserts a wait
void TskDeAssertWaitQueue (TskWaitQueue_t* queue, ipl_t ipl);

#define TSK_TIMEOUT_NONE 0

// Wait flags
#define TSK_WAIT_ASSERTED \
    (1 << 0)    // Specifies that wait is already asserted
                // Use with care

#endif

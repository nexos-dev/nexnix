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
#include <nexke/lock.h>
#include <nexke/types.h>
#include <nexke/wait.h>
#include <stdbool.h>

// Thread entry type
typedef void (*NkThreadEntry) (void*);

// Wait obejct
// Defines an object this thread is waiting on, and is used to create priority inheritance chains
typedef struct _waitobj
{
    NkLink_t link;
    NkThread_t* owner;     // Owner of this object
    NkThread_t* waiter;    // Waiter
    int type;              // Type of object being waited on
    ktime_t timeout;       // Timeout of this object
    void* obj;             // Pointer to object being waited on
    int result;
    spinlock_t lock;
} TskWaitObj_t;

#define TSK_THREAD_MAX_WAIT 4

#define TSK_WAITOBJ_TIMER     0
#define TSK_WAITOBJ_MSG       1
#define TSK_WAITOBJ_SEMAPHORE 2
#define TSK_WAITOBJ_CONDITION 3
#define TSK_WAITOBJ_MUTEX     4
#define TSK_WAITOBJ_QUEUE     5

#define TSK_WAITOBJ_IN_PROG    0
#define TSK_WAITOBJ_SUCCESS    1
#define TSK_WAITOBJ_TIMEOUT    2
#define TSK_WAITOBJ_OWNER_DIED 3

// Thread structure
typedef struct _thread
{
    NkLink_t link;    // Link in ready queue / wait lists
                      // At head so we don't have to use LINK_CONTAINER
    // Thread identity info
    id_t tid;             // Thread ID
    const char* name;     // Name of thread
    int priority;         // Priority of this thread
    int state;            // State of this thread
    int flags;            // Flags for this thread
    atomic_t refCount;    // Things referencing this thread
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
    int exitCode;    // Code thread exited with
    // Wait info
    TskWaitObj_t wait;           // Object we are waiting on
    TskWaitObj_t timer;          // Timer we are waiting on
    TskWaitQueue_t joinQueue;    // Threads joined to this thread
    // Thread flags
    bool preempted;               // Wheter this thread has been preempted
    bool timeoutPending;          // Wheter a timeout is pending
    volatile int waitAsserted;    // Wheter a wait is asserted on this thread
    NkTimeEvent_t* timeout;       // Wait queue timeout
} NkThread_t;

// Thread flags
#define TSK_THREAD_IDLE (1 << 0)

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
#define TSK_THREAD_READY       0
#define TSK_THREAD_RUNNING     1
#define TSK_THREAD_WAITING     2
#define TSK_THREAD_CREATED     3
#define TSK_THREAD_TERMINATING 4

// Maybe this should be bigger
#define NEXKE_MAX_THREAD 8192

// Initializes task system
void TskInitSys();

// Initializes scheduler
void TskInitSched();

// Creates a new thread object
NkThread_t* TskCreateThread (NkThreadEntry entry, void* arg, const char* name, int flags);

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

// Asserts and sets up a wait
// IPL must be raised and object must be locked
TskWaitObj_t* TskAssertWait (NkThread_t* objOwner, ktime_t timeout, void* obj, int type);

// Wait on a wait object
// Returns true if wait was successful, false if it failed
bool TskWaitOnObj (TskWaitObj_t* waitObj);

// Clears a wait on a wait object
// If timeout already expired, returns false
bool TskClearWait (TskWaitObj_t* waitObj, int result);

// Wakes up a wait object
void TskWakeObj (TskWaitObj_t* obj);

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

// Refs a thread
static inline void TskRefThread (NkThread_t* thread)
{
    NkAtomicAdd (&thread->refCount, 1);
}

// Gets current thread
NkThread_t* TskGetCurrentThread();

// Yields from current thread
// Safe wrapper over TskSchedule
void TskYield();

// Starts a thread up
void TskStartThread (NkThread_t* thread);

// Makes thread sleep for specified amount of time
void TskSleepThread (ktime_t time);

// Gets a thread's argument
void* TskGetThreadArg (NkThread_t* thread);

// Terminates current thread
void TskTerminateSelf (int code);

// Waits for thread termination
errno_t TskJoinThread (NkThread_t* thread);

// Waits for thread termination with time out
errno_t TskJoinThreadTimeout (NkThread_t* thread, ktime_t timeout);

// Quantum stuff

// Time slicer operating delta (in ns)
#define TSK_TIMESLICE_DELTA 10000000

// Default time slice length (in time slicer ticks)
#define TSK_TIMESLICE_LEN 6    // equals 60 ms

#endif

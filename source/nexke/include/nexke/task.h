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

// Thread entry type
typedef void (*NkThreadEntry) (void*);

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
    // Thread flags
    bool preempted;     // Wheter this thread has been preempted
    spinlock_t lock;    // Thread lock
} NkThread_t;

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

// Unblocks a thread
void TskUnblockThread (NkThread_t* thread);

// Preempts current thread
void TskPreempt();

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

// Quantum stuff

// Time slicer operating delta (in ns)
#define TSK_TIMESLICE_DELTA 10000000

// Default time slice length (in time slicer ticks)
#define TSK_TIMESLICE_LEN 6    // equals 60 ms

#endif

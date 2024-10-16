/*
    sched.c - contains thread scheduler for nexke
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

#include <assert.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/task.h>
#include <string.h>

// Globals (try to avoid these)
static PltHwClock_t* clock = NULL;

// NOTE: all routines in here are interrupt-unsafe, but do not actually disable interrupts
// It's the caller's responsibilty to disable interrupts

// NOTE2: we use the list inlines differently here. So that we don't have to LINK_CONTAINER
// we keep the link at the start of the thread structure and simple cast back and forth
// This helps performance some

// Idle thread routine
static void TskIdleThread (void*)
{
    for (;;)
        CpuHalt();
}

// Admits thread to ready queue
// If this thread was preempted, it's added to the front;
// otherwise, its added to the tail
static FORCEINLINE void tskReadyThread (NkCcb_t* ccb, NkThread_t* thread)
{
    thread->state = TSK_THREAD_READY;
    if (thread->preempted)
    {
        thread->preempted = false;    // Reset flag as preemption doesn't matter anymore
        NkListAddFront (&ccb->readyQueue, &thread->link);
    }
    else
    {
        NkListAddBack (&ccb->readyQueue, &thread->link);    // For FCFS
    }
}

// Hook to prepare thread to stop running and let another thread run
static FORCEINLINE void tskStopThread (NkCcb_t* ccb, NkThread_t* thread)
{
    // Figure out state
    if (thread->state == TSK_THREAD_RUNNING)
        tskReadyThread (ccb, thread);    // Admit it to run queue
    // Update runtime of thread
    thread->runTime += (clock->getTime() - thread->lastSchedule);
}

// Sets the current thread
// NOTE: call with interrupts disabled
static FORCEINLINE void tskSetCurrentThread (NkCcb_t* ccb, NkThread_t* thread)
{
    // Get current thread
    NkThread_t* oldThread = ccb->curThread;
    assert (oldThread);
    // Set new thread as current
    thread->state = TSK_THREAD_RUNNING;
    ccb->curThread = thread;
    // Stop the thread
    tskStopThread (ccb, oldThread);
    // Set last schedule time
    thread->lastSchedule = clock->getTime();
    // Do a context swap
    CpuSwitchContext (thread->context, &oldThread->context);
    // NOTE: from the CPU's perspective, we return from CpuSwitchContext in the new thread
    // From oldThread's perspective, it pauses, and then whenever it gets queue again, returns here
    // From thread's perspective, it resumes from a previous pause here
    // This is the only place where CpuSwitchContext gets called in the system
    // (besides the below function)
}

// Schedules a thread to execute
// The main interface to the scheduler
// NOTE: interrupt unsafe, call with IPL raised
static FORCEINLINE void tskSchedule (NkCcb_t* ccb)
{
    // Grab next thread and current thread
    NkThread_t* nextThread = (NkThread_t*) NkListFront (&ccb->readyQueue);
    NkListRemove (&ccb->readyQueue, (NkLink_t*) nextThread);
    if (!nextThread)
    {
        // We either keep goinf or idle
        // This depends on the thread's state
        if (ccb->curThread->state == TSK_THREAD_RUNNING)
            return;                      // Don't do anything
        nextThread = ccb->idleThread;    // Run idle thread
    }
    // Execute the thread
    tskSetCurrentThread (ccb, nextThread);
}

// Performs the initial task switch
// Basically the same as above, just assumes there isn't and old thread
// And is callable by the outside world
void __attribute__ ((noreturn)) TskSetInitialThread (NkThread_t* thread)
{
    NkCcb_t* ccb = CpuGetCcb();
    thread->state = TSK_THREAD_RUNNING;
    // Set last schedule time
    thread->lastSchedule = clock->getTime();
    // Set as current
    ccb->curThread = thread;
    CpuContext_t* fakeCtx = NULL;    // Fake context pointer
    CpuSwitchContext (thread->context, &fakeCtx);
    // UNREACHABLE
    assert (0);
}

// Inline function wrappers
void TskSetCurrentThread (NkThread_t* thread)
{
    tskSetCurrentThread (CpuGetCcb(), thread);
}

void TskReadyThread (NkThread_t* thread)
{
    tskReadyThread (CpuGetCcb(), thread);
}

void TskSchedule()
{
    tskSchedule (CpuGetCcb());
}

// Initializes scheduler
void TskInitSched()
{
    // Create the idle thread
    NkCcb_t* ccb = CpuGetCcb();
    ccb->idleThread = TskCreateThread (TskIdleThread, NULL, "TskIdleThread");
    assert (ccb->idleThread);
    // Get clock
    clock = PltGetPlatform()->clock;
}

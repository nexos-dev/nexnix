/*
    thread.c - contains thread manager for nexke
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

#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/task.h>
#include <string.h>

// System thread table
static NkThread_t* nkThreadTable[NEXKE_MAX_THREAD] = {0};

// Thread caching/resource info
static SlabCache_t* nkThreadCache = NULL;
static NkResArena_t* nkThreadRes = NULL;

// Thread terminator work queue (pun intended)
static NkWorkQueue_t* nkTerminator = NULL;

#define NK_TERMINATOR_THRESHOLD 1

// Standard thread entry point
static void TskThreadEntry()
{
    // Get the thread structure
    NkThread_t* thread = CpuGetCcb()->curThread;
    // Unlock ready queue, as we start with it locked
    if (CpuGetCcb()->preemptDisable)
        NkSpinUnlock (&CpuGetCcb()->rqLock);
    // Make IPL is at low level
    PltLowerIpl (PLT_IPL_LOW);
    // Executte entry
    thread->entry (thread->arg);
}

// Thread terminator
static void TskTerminator (NkWorkItem_t* item)
{
    NkThread_t* thread = (NkThread_t*) item->data;
    assert (thread->state == TSK_THREAD_TERMINATING);
    TskDestroyThread (thread);
}

// Creates a new thread object
NkThread_t* TskCreateThread (NkThreadEntry entry, void* arg, const char* name, int flags)
{
    // Allocate a thread
    NkThread_t* thread = MmCacheAlloc (nkThreadCache);
    // Allocate a thread table entry
    id_t tid = NkAllocResource (nkThreadRes);
    if (tid == -1)
    {
        // Creation failed
        MmCacheFree (nkThreadCache, thread);
        return NULL;
    }
    // Setup thread
    memset (thread, 0, sizeof (NkThread_t));
    thread->arg = arg;
    thread->name = name;
    thread->entry = entry;
    thread->tid = tid;
    thread->refCount = 1;
    thread->flags = flags;
    TskInitWaitQueue (&thread->joinQueue, TSK_WAITOBJ_QUEUE);
    // Initialize CPU specific context
    thread->context = CpuAllocContext ((uintptr_t) TskThreadEntry);
    if (!thread->context)
    {
        // Failure
        NkFreeResource (nkThreadRes, tid);
        MmCacheFree (nkThreadCache, thread);
        return NULL;
    }
    // Setup scheduling info
    thread->state = TSK_THREAD_CREATED;
    thread->quantum = TSK_TIMESLICE_LEN;
    thread->timeout = NkTimeNewEvent();
    if (!thread->timeout)
    {
        // Failure
        CpuDestroyContext (thread->context);
        NkFreeResource (nkThreadRes, tid);
        MmCacheFree (nkThreadCache, thread);
        return NULL;
    }
    // Add to table
    nkThreadTable[tid] = thread;
    return thread;
}

// Terminates ourself
void TskTerminateSelf (int code)
{
    // Lock the scheduler
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkThread_t* thread = TskGetCurrentThread();
    assert (thread->state == TSK_THREAD_RUNNING);
    // Set our state and exit code
    thread->state = TSK_THREAD_TERMINATING;
    thread->exitCode = code;
    // Awake all joined threads
    TskBroadcastWaitQueue (&thread->joinQueue, 0);
    // Close it in case someone else trys to join before being destroyed
    TskCloseWaitQueue (&thread->joinQueue, 0);
    // Add to terminator list
    NkWorkQueueSubmit (nkTerminator, (void*) thread);
    // Now schedule another thread
    TskSchedule();
    // UNREACHABLE
}

// Destroys a thread object
void TskDestroyThread (NkThread_t* thread)
{
    // Destory memory structures if ref count is 0
    if (NkAtomicSub (&thread->refCount, 1) == 0)
    {
        NkTimeFreeEvent (thread->timeout);
        CpuDestroyContext (thread->context);
        NkFreeResource (nkThreadRes, thread->tid);
        MmCacheFree (nkThreadCache, thread);
    }
}

// Asserts and sets up a wait
// IPL must be raised and object must be locked
// Locks the current thread while asserting the wait
TskWaitObj_t* TskAssertWait (NkThread_t* objOwner, ktime_t timeout, void* obj, int type)
{
    // Get current thread
    NkCcb_t* ccb = CpuGetCcb();
    NkThread_t* thread = ccb->curThread;
    // Ensure we aren't already waiting on something else
    assert (thread->state != TSK_THREAD_WAITING && !thread->waitAsserted);
    thread->state = TSK_THREAD_WAITING;    // Assert the wait
    TSK_SET_THREAD_ASSERT (thread, 1);
    // Prepare the wait object
    TskWaitObj_t* waitObj = &thread->wait;
    waitObj->obj = obj;
    waitObj->type = type;
    waitObj->waiter = thread;
    waitObj->timeout = timeout;
    // Setup a timeout
    if (timeout)
    {
        thread->timeoutPending = true;
        NkTimeSetWakeEvent (thread->timeout, thread);
        NkTimeRegEvent (thread->timeout, timeout, 0);
    }
    return waitObj;
}

// Finishes a wait on a wait object
bool TskFinishWait (TskWaitObj_t* waitObj)
{
    // Check if timeout expired
    NkThread_t* thread = waitObj->waiter;
    if (waitObj->timeout && thread->timeout->expired)
        return true;    // Timeout expired
    return false;
}

// Clears a wait on a wait object
// If timeout already expired, returns false
bool TskClearWait (TskWaitObj_t* waitObj)
{
    // De-register it's timeout if it's pending
    if (waitObj->timeout)
    {
        NkThread_t* thread = waitObj->waiter;
        NkTimeDeRegEvent (thread->timeout);
        // Now check for expiry
        // This prevents race conditions where the thread gets readied by the timeout but hasn't
        // been scheduled yet. In that case we still see the timeout as pending
        // but we can't ready the thread as that would be bad
        if (thread->timeout->expired)
            return true;
    }
    return false;
}

// Yields from current thread
// Safe wrapper over TskSchedule
void TskYield()
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    TskSchedule();
    PltLowerIpl (ipl);
}

// Starts a thread up
void TskStartThread (NkThread_t* thread)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    TskReadyThread (thread);
    PltLowerIpl (ipl);
}

// Makes thread sleep for specified amount of time
void TskSleepThread (ktime_t time)
{
    // Create a wait queue with a timeout for this
    TskWaitQueue_t queue = {0};
    TskInitWaitQueue (&queue, TSK_WAITOBJ_QUEUE);
    TskWaitQueueTimeout (&queue, time);
}

// Gets a thread's argument
void* TskGetThreadArg (NkThread_t* thread)
{
    return thread->arg;
}

// Waits for thread termination
errno_t TskJoinThread (NkThread_t* thread)
{
    TskRefThread (thread);
    errno_t err = TskWaitQueue (&thread->joinQueue);
    // Destroy the thread if we should
    if (err == EOK)
        TskDestroyThread (thread);
    return err;
}

// Waits for thread termination with time out
errno_t TskJoinThreadTimeout (NkThread_t* thread, ktime_t timeout)
{
    TskRefThread (thread);
    errno_t err =
        TskWaitQueueTimeout (&thread->joinQueue, timeout);    // Destroy the thread if we should
    if (err == EOK)
        TskDestroyThread (thread);
    return err;
}

// Initializes task system
void TskInitSys()
{
    NkLogDebug ("nexke: initializing multitasking\n");
    // Create cache and resource
    nkThreadCache = MmCacheCreate (sizeof (NkThread_t), "NkThread_t", 0, 0);
    nkThreadRes = NkCreateResource ("NkThread", 0, NEXKE_MAX_THREAD - 1);
    assert (nkThreadCache && nkThreadRes);
    TskInitSched();
    nkTerminator = NkWorkQueueCreate (TskTerminator, NK_WORK_DEMAND, 0, 0, NK_TERMINATOR_THRESHOLD);
}

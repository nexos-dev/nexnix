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
static spinlock_t nkThreadsLock = 0;

// Thread caching/resource info
static SlabCache_t* nkThreadCache = NULL;
static NkResArena_t* nkThreadRes = NULL;

// Thread terminator work queue (pun intended)
static NkWorkQueue_t* nkTerminator = NULL;

#define NK_TERMINATOR_THRESHOLD 5

// Standard thread entry point
static void TskThreadEntry()
{
    // Get the thread structure
    NkThread_t* thread = CpuGetCcb()->curThread;
    // Unlock ready queue, as we start with it locked
    NkCcb_t* ccb = CpuGetCcb();
    if (ccb->preemptDisable)
        TskUnlockRq (ccb);
    // Make sure IPL is at low level
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
NkThread_t* TskCreateThread (NkThreadEntry entry,
                             void* arg,
                             const char* name,
                             int policy,
                             int prio,
                             int flags)
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
    thread->policy = policy;
    if (thread->priority)
        thread->priority = prio;
    else
        thread->priority = tskPrioTable[policy];
    // Set flags of policy
    if (policy == TSK_POLICY_FIFO)
        thread->flags |= (TSK_THREAD_FIFO | TSK_THREAD_FIXED_PRIO);
    else if (policy == TSK_POLICY_RR)
        thread->flags |= TSK_THREAD_FIXED_PRIO;
    TskInitWaitQueue (&thread->joinQueue, TSK_WAITOBJ_QUEUE);
    NkListInit (&thread->ownedWaits);
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
    NkSpinLock (&nkThreadsLock);
    nkThreadTable[tid] = thread;
    NkSpinUnlock (&nkThreadsLock);
    return thread;
}

// Terminates ourself
void TskTerminateSelf (int code)
{
    // Lock the scheduler
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkThread_t* thread = TskGetCurrentThread();
    TskLockThread (thread);
    assert (thread->state == TSK_THREAD_RUNNING);
    // Set our state and exit code
    thread->state = TSK_THREAD_TERMINATING;
    thread->exitCode = code;
    // Awake all joined threads
    TskBroadcastWaitQueue (&thread->joinQueue, 0);
    // Close it in case someone else trys to join before being destroyed
    TskCloseWaitQueue (&thread->joinQueue, 0);
    // Wake all owned wait objects
    NkLink_t* iter = NkListFront (&thread->ownedWaits);
    while (iter)
    {
        TskWaitObj_t* obj = LINK_CONTAINER (iter, TskWaitObj_t, ownerLink);
        if (obj->waiter)
        {
            bool wakeSucess = TskClearWait (obj, TSK_WAITOBJ_SUCCESS);
            if (wakeSucess)
                TskWakeObj (obj);
        }
        iter = NkListIterate (&thread->ownedWaits, iter);
    }
    // Add to terminator list if and only if dropping the reference count would cause thread
    // destruction Other wise we don't need to This is because somebody still needs the thread
    // structure so making the work queue simple decrement a variable when it's not actually going
    // to destroy it is unecessary
    if (thread->refCount == 1)
        NkWorkQueueSubmit (nkTerminator, (void*) thread);
    else
        --thread->refCount;
    TskUnlockThread (thread);
    // Now schedule another thread
    TskSchedule();
    // UNREACHABLE
}

// Destroys a thread object
void TskDestroyThread (NkThread_t* thread)
{
    // Destory memory structures if ref count is 0
    NkSpinLock (&nkThreadsLock);
    TskLockThread (thread);
    if (--thread->refCount == 0)
    {
        // Remove from table
        nkThreadTable[thread->tid] = NULL;
        // Destroy all components of thread
        NkTimeFreeEvent (thread->timeout);
        CpuDestroyContext (thread->context);
        NkFreeResource (nkThreadRes, thread->tid);
        MmCacheFree (nkThreadCache, thread);
    }
    else
        TskUnlockThread (thread);
    NkSpinUnlock (&nkThreadsLock);
}

// Asserts and sets up a wait
// IPL must be raised and object must be locked
// Locks the current thread while asserting the wait
TskWaitObj_t* TskAssertWait (ktime_t timeout, void* obj, int type)
{
    // Get current thread
    NkCcb_t* ccb = CpuGetCcb();
    NkThread_t* thread = ccb->curThread;
    // Ensure we aren't already waiting on something else
    assert (thread->state != TSK_THREAD_WAITING && !thread->waitAsserted);
    thread->state = TSK_THREAD_WAITING;    // Assert the wait
    TskThreadSetAssert (thread, 1);
    // Prepare the wait object
    TskWaitObj_t* waitObj = &thread->wait;
    waitObj->obj = obj;
    waitObj->type = type;
    waitObj->waiter = thread;
    waitObj->timeout = timeout;
    waitObj->result = TSK_WAITOBJ_IN_PROG;
    // Setup a timeout
    if (timeout)
    {
        thread->timeoutPending = true;
        NkTimeSetWakeEvent (thread->timeout, waitObj);
        NkTimeRegEvent (thread->timeout, timeout, 0);
    }
    return waitObj;
}

// Wait on a wait object
// Returns true if wait was successful, false if it failed
bool TskWaitOnObj (TskWaitObj_t* waitObj, int flags)
{
    // Wait
    TskSchedule();
    if (waitObj->result != TSK_WAITOBJ_SUCCESS)
        return false;
    if (flags & TSK_WAITOBJ_OWN)
    {
        // Gain ownership now
        NkThread_t* thread = TskGetCurrentThread();
        NkListAddFront (&thread->ownedWaits, &waitObj->ownerLink);
        waitObj->owner = thread;
    }
    else
        waitObj->owner = NULL;
    return true;
}

// Clears a wait on a wait object
// If wakeup already occured, returns false
bool TskClearWait (TskWaitObj_t* waitObj, int result)
{
    TskLockThread (waitObj->waiter);
    // Make sure a wait isn't asserted
    TskThreadCheckAssert (waitObj->waiter);
    // Check status of wait
    if (waitObj->result != TSK_WAITOBJ_IN_PROG)
    {
        TskUnlockThread (waitObj->waiter);
        return false;    // Objects already awoken
    }
    // De-register any pending time events
    if (result != TSK_WAITOBJ_TIMEOUT && waitObj->timeout)
        NkTimeDeRegEvent (waitObj->waiter->timeout);
    waitObj->result = result;
    TskUnlockThread (waitObj->waiter);
    return true;    // We return a locked wait object
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

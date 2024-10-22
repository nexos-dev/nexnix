/*
    wait.c - implements wait queues
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

// Initializes a wait queue
void TskInitWaitQueue (TskWaitQueue_t* queue, int object)
{
    memset (queue, 0, sizeof (TskWaitQueue_t));
    queue->queueObject = object;
}

// Asserts that we are going to wait
// Returns old IPL pre-assert
ipl_t TskAssertWaitQueue (TskWaitQueue_t* queue)
{
    // Protect the queue now
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);    // Disable interrupts
    NkSpinLock (&queue->lock);
    return ipl;
}

// Deasserts a wait
void TskDeAssertWaitQueue (TskWaitQueue_t* queue, ipl_t ipl)
{
    NkSpinUnlock (&queue->lock);
    PltLowerIpl (ipl);
}

// Waits with flags
// Main waiting function
errno_t TskWaitQueueFlags (TskWaitQueue_t* queue, int flags, ktime_t timeout)
{
    ipl_t ipl = 0;
    // Check if we need to assert
    if (!(flags & TSK_WAIT_ASSERTED))
        ipl = TskAssertWaitQueue (queue);    // Assert that we are about to wait
    errno_t err = EOK;
    // Check if we are open
    if (queue->done)
    {
        err = EAGAIN;
        goto cleanup;
    }
    // Now we can prepare to wait
    TskWaitObj_t* waitObj =
        TskAssertWait (TskGetCurrentThread(), timeout, queue, queue->queueObject);
    assert (waitObj);
    // Add to sleepers
    NkListAddBack (&queue->waiters, &waitObj->link);
    // Block now, but first unlock the queue
    NkSpinUnlock (&queue->lock);
    bool waitStatus = TskWaitOnObj (waitObj);
    NkSpinLock (&queue->lock);
    if (!waitStatus)
    {
        // Wait failed, cleanup
        NkListRemove (&queue->waiters, &waitObj->link);
        NkSpinUnlock (&waitObj->lock);
        // Timeout occured, return error
        err = ETIMEDOUT;
        goto cleanup;
    }
    NkSpinUnlock (&waitObj->lock);    // Unlock wait object (as WaitOnObj locked it)
    // Check if we were awoken as the result of closure
    if (queue->done)
    {
        err = EAGAIN;
        goto cleanup;
    }
cleanup:
    // Deassert if we need to
    if (!(flags & TSK_WAIT_ASSERTED))
        TskDeAssertWaitQueue (queue, ipl);
    return err;
}

// Waits with timeout
errno_t TskWaitQueueTimeout (TskWaitQueue_t* queue, ktime_t timeout)
{
    return TskWaitQueueFlags (queue, 0, timeout);
}

// Waits on queue
errno_t TskWaitQueue (TskWaitQueue_t* queue)
{
    return TskWaitQueueFlags (queue, 0, 0);
}

// Unsafe function to wake a thread off the wait queue
static FORCEINLINE void tskWakeThread (TskWaitQueue_t* queue, TskWaitObj_t* waitObj)
{
    // Clear the wait
    bool wakeSuccess = TskClearWait (waitObj, TSK_WAITOBJ_SUCCESS);
    if (wakeSuccess)
    {
        // Remove it from list
        NkListRemove (&queue->waiters, &waitObj->link);
        // Now ready the new thread of needed (which may request preemption)
        TskWakeObj (waitObj);
    }
}

// Unsafe function to wake all threads off the queue
static FORCEINLINE void tskWakeQueue (TskWaitQueue_t* queue)
{
    // Iterate through list
    while (NkListFront (&queue->waiters))
    {
        TskWaitObj_t* waiter = (TskWaitObj_t*) NkListFront (&queue->waiters);
        // Wake it
        tskWakeThread (queue, waiter);
    }
}

// Wakes a thread off the wait queue
errno_t TskWakeWaitQueue (TskWaitQueue_t* queue, int flags)
{
    ipl_t ipl = 0;
    errno_t err = EOK;
    // Check if we need to assert
    if (!(flags & TSK_WAIT_ASSERTED))
        ipl = TskAssertWaitQueue (queue);    // Assert that we are about to wait
    if (queue->done)
    {
        err = EAGAIN;
        goto cleanup;
    }
    // Get first sleeping thread
    TskWaitObj_t* waiter = (TskWaitObj_t*) NkListFront (&queue->waiters);
    if (waiter)
        tskWakeThread (queue, waiter);    // Wake the thread
cleanup:
    // Deassert if we need to
    if (!(flags & TSK_WAIT_ASSERTED))
        TskDeAssertWaitQueue (queue, ipl);
    return err;
}

// Wakes up an entire wait queue
errno_t TskBroadcastWaitQueue (TskWaitQueue_t* queue, int flags)
{
    ipl_t ipl = 0;
    errno_t err = EOK;
    // Check if we need to assert
    if (!(flags & TSK_WAIT_ASSERTED))
        ipl = TskAssertWaitQueue (queue);    // Assert that we are about to wait
    if (queue->done)
    {
        err = EAGAIN;
        goto cleanup;
    }
    // Wake the entire queue
    tskWakeQueue (queue);
cleanup:
    // Deassert if we need to
    if (!(flags & TSK_WAIT_ASSERTED))
        TskDeAssertWaitQueue (queue, ipl);
    return err;
}

// Closes a wait queue and broadcasts the closing
errno_t TskCloseWaitQueue (TskWaitQueue_t* queue, int flags)
{
    ipl_t ipl = 0;
    errno_t err = EOK;
    // Check if we need to assert
    if (!(flags & TSK_WAIT_ASSERTED))
        ipl = TskAssertWaitQueue (queue);    // Assert that we are about to wait
    if (queue->done)
    {
        err = EAGAIN;
        goto cleanup;
    }
    // Wake the entire queue and close it
    queue->done = true;
    tskWakeQueue (queue);
cleanup:
    // Deassert if we need to
    if (!(flags & TSK_WAIT_ASSERTED))
        TskDeAssertWaitQueue (queue, ipl);
    return err;
}

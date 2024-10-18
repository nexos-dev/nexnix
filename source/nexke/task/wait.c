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
void TskInitWaitQueue (TskWaitQueue_t* queue, int count)
{
    memset (queue, 0, sizeof (TskWaitQueue_t));
    queue->pendingWaits = count;
}

// Asserts that we are going to wait
// Returns old IPL pre-assert
ipl_t TskWaitQueueAssert (TskWaitQueue_t* queue)
{
    // Protect the queue now
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);    // Disable interrupts
    NkSpinLock (&queue->lock);
    return ipl;
}

// Deasserts a wait
void TskWaitQueueDeassert (TskWaitQueue_t* queue, ipl_t ipl)
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
        ipl = TskWaitQueueAssert (queue);    // Assert that we are about to wait
    errno_t err = EOK;
    // Check if we are open
    if (queue->done)
    {
        err = EAGAIN;
        goto cleanup;
    }
    // Check if we need to block
    if (queue->pendingWaits > 0)
    {
        --queue->pendingWaits;    // Decrement counter
        goto cleanup;
    }
    // Check if we're able to block
    // At this point if we can't block fail
    if (flags & TSK_WAIT_NO_BLOCK)
    {
        err = EWOULDBLOCK;
        goto cleanup;
    }
    // Now we can prepare to wait
    TskWaitObj_t* waitObj =
        TskAssertWait (TskGetCurrentThread(), timeout, queue, TSK_WAITOBJ_QUEUE);
    assert (waitObj);
    // Add to sleepers
    NkListAddBack (&queue->waiters, &waitObj->link);
    // Block now, but first unlock the queue
    NkSpinUnlock (&queue->lock);
    TskBlockThread();
    NkSpinLock (&queue->lock);    // Re-lock queue
    // We get here after we were woken up
    // Clear the wait, which will check if we timed out
    if (!TskFinishWait (waitObj))
    {
        // Cleanup
        NkListRemove (&queue->waiters, &waitObj->link);
        // Timeout occured, return error
        err = ETIMEDOUT;
        goto cleanup;
    }
cleanup:
    // Deassert if we need to
    if (!(flags & TSK_WAIT_ASSERTED))
        TskWaitQueueDeassert (queue, ipl);
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
    bool timeExpired = TskClearWait (waitObj);
    // Remove it from list
    NkListRemove (&queue->waiters, &waitObj->link);
    // Now ready the new thread of needed (which may request preemption)
    if (!timeExpired)
        TskReadyThread (waitObj->waiter);
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

// Signals wait queue
// Doesn't affect pendingWaits if there are no threads
void TskSignalWaitQueue (TskWaitQueue_t* queue)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&queue->lock);
    if (!queue->done)
    {
        // Get first waiter
        TskWaitObj_t* waiter = (TskWaitObj_t*) NkListFront (&queue->waiters);
        if (waiter)
            tskWakeThread (queue, waiter);    // Wake the thread
    }
    NkSpinUnlock (&queue->lock);
    PltLowerIpl (ipl);
}

// Wakes a thread off the wait queue
void TskWakeWaitQueue (TskWaitQueue_t* queue)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&queue->lock);
    if (!queue->done)
    {
        // Get first sleeping thread
        TskWaitObj_t* waiter = (TskWaitObj_t*) NkListFront (&queue->waiters);
        if (waiter)
            tskWakeThread (queue, waiter);    // Wake the thread
        else
            ++queue->pendingWaits;    // Increase number of waits
    }
    // Cleanup
    NkSpinUnlock (&queue->lock);
    PltLowerIpl (ipl);
}

// Wakes up an entire wait queue
void TskBroadcastWaitQueue (TskWaitQueue_t* queue)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&queue->lock);
    if (!queue->done)
    {
        // Wake the entire queue
        tskWakeQueue (queue);
    }
    // Cleanup
    NkSpinUnlock (&queue->lock);
    PltLowerIpl (ipl);
}

// Closes a wait queue and broadcasts the closing
void TskCloseWaitQueue (TskWaitQueue_t* queue)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkSpinLock (&queue->lock);
    // Wake the entire queue and close it
    tskWakeQueue (queue);
    queue->done = true;
    // Cleanup
    NkSpinUnlock (&queue->lock);
    PltLowerIpl (ipl);
}

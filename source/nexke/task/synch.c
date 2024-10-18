/*
    synch.c - contains implementation of synchronization objects
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
#include <nexke/synch.h>
#include <nexke/task.h>

// Semaphore implementation

// Initializes a semaphore
void TskInitSemaphore (TskSemaphore_t* sem, int count)
{
    TskInitWaitQueue (&sem->queue, TSK_WAITOBJ_SEMAPHORE);
    sem->count = count;
}

// Acquires a semaphore
errno_t TskAcquireSemaphore (TskSemaphore_t* sem)
{
    // Assert the wait
    ipl_t ipl = TskAssertWaitQueue (&sem->queue);
    errno_t err = EOK;
    // This is a loop as between release and wakeup someone else could have
    // acquired the semaphore
    while (sem->count <= 0 && err == EOK)
        err = TskWaitQueueFlags (&sem->queue, TSK_WAIT_ASSERTED, 0);
    // Decrement count if we suceeded
    if (err == EOK)
        --sem->count;
    // De assert the wait
    TskDeAssertWaitQueue (&sem->queue, ipl);
    return err;
}

// Releases a semaphore
errno_t TskReleaseSemaphore (TskSemaphore_t* sem)
{
    // Assert the wait queue
    ipl_t ipl = TskAssertWaitQueue (&sem->queue);
    errno_t err = EOK;
    ++sem->count;
    if (sem->count > 0)
        err = TskWakeWaitQueue (&sem->queue, TSK_WAIT_ASSERTED);    // Wake someone
    TskDeAssertWaitQueue (&sem->queue, ipl);
    return err;
}

// Attempts to lock a semaphore
errno_t TskTryAcquireSemaphore (TskSemaphore_t* sem)
{
    // Assert the queue
    ipl_t ipl = TskAssertWaitQueue (&sem->queue);
    errno_t err = EOK;
    if (sem->count <= 0)
        err = EWOULDBLOCK;
    // Decrement count if we are able
    if (err == EOK)
        --sem->count;
    TskDeAssertWaitQueue (&sem->queue, ipl);
    return err;
}

// Closes a semaphore
errno_t TskCloseSemaphore (TskSemaphore_t* sem)
{
    return TskCloseWaitQueue (&sem->queue, 0);
}

// Mutex implementation

// Initializes a mutex
void TskInitMutex (TskMutex_t* mtx)
{
    TskInitWaitQueue (&mtx->queue, TSK_WAITOBJ_MUTEX);
    mtx->state = false;
}

// Acquires a mutex
errno_t TskAcquireMutex (TskMutex_t* mtx)
{
    ipl_t ipl = TskAssertWaitQueue (&mtx->queue);
    errno_t err = EOK;
    while (mtx->state && err == EOK)
        err = TskWaitQueueFlags (&mtx->queue, TSK_WAIT_ASSERTED, 0);
    if (err == EOK)
        mtx->state = true;
    TskDeAssertWaitQueue (&mtx->queue, ipl);
    return err;
}

// Releases a mutex
errno_t TskReleaseMutex (TskMutex_t* mtx)
{
    ipl_t ipl = TskAssertWaitQueue (&mtx->queue);
    errno_t err = EOK;
    assert (mtx->state);
    mtx->state = false;
    err = TskWakeWaitQueue (&mtx->queue, TSK_WAIT_ASSERTED);
    TskDeAssertWaitQueue (&mtx->queue, ipl);
    return err;
}

// Tries to acquire a mutex
errno_t TskTryAcquireMutex (TskMutex_t* mtx)
{
    // Assert the queue
    ipl_t ipl = TskAssertWaitQueue (&mtx->queue);
    errno_t err = EOK;
    if (mtx->state)
        err = EWOULDBLOCK;
    // Decrement count if we are able
    if (err == EOK)
        mtx->state = true;
    TskDeAssertWaitQueue (&mtx->queue, ipl);
    return err;
}

// Closes a mutex
errno_t TskCloseMutex (TskMutex_t* mtx)
{
    return TskCloseWaitQueue (&mtx->queue, 0);
}

// Condition variables

// Initializes a condition
void TskInitCondition (TskCondition_t* cond)
{
    TskInitWaitQueue (&cond->queue, TSK_WAITOBJ_CONDITION);
}

// Waits on a condition, taking a mutex to unlock before blocking
// to prevent lost-wakeup
errno_t TskWaitCondition (TskCondition_t* cond, TskMutex_t* mtx)
{
    ipl_t ipl = TskAssertWaitQueue (&cond->queue);
    TskReleaseMutex (mtx);    // Relelase the mutex now that we are locked to prevent lost wakeup
    errno_t err = TskWaitQueueFlags (&cond->queue, TSK_WAIT_ASSERTED, 0);
    TskDeAssertWaitQueue (&cond->queue, ipl);
    return err;
}

// Signals a thread to wake up on condition
errno_t TskSignalCondition (TskCondition_t* cond)
{
    return TskWakeWaitQueue (&cond->queue, 0);
}

// Broadcasts a condition
errno_t TskBroadcastCondition (TskCondition_t* cond)
{
    ipl_t ipl = TskAssertWaitQueue (&cond->queue);
    errno_t err = TskBroadcastWaitQueue (&cond->queue, TSK_WAIT_ASSERTED);
    if (err == EOK)
    {
        // Close it if broadcast succeeded
        err = TskCloseWaitQueue (&cond->queue, TSK_WAIT_ASSERTED);
    }
    TskDeAssertWaitQueue (&cond->queue, ipl);
    return err;
}

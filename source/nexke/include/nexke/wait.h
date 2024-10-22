/*
    wait.h - contains wait queue stuff
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

#ifndef _WAIT_H
#define _WAIT_H

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
#define TSK_WAIT_NOT_OWNER \
    (1 << 1)    // Specifies that when wait is over we will not own the object

#endif

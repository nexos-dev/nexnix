/*
    synch.h - contains synchronization objects
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

#ifndef _SYNCH_H
#define _SYNCH_H

#include <nexke/task.h>
#include <stdbool.h>

// Semaphores
typedef struct _semaphore
{
    TskWaitQueue_t queue;    // Semaphore wait queue
    int count;               // Count of semaphore
} TskSemaphore_t;

// Initializes a semaphore
void TskInitSemaphore (TskSemaphore_t* sem, int count);

// Acquires a semaphore
errno_t TskAcquireSemaphore (TskSemaphore_t* sem);

// Releases a semaphore
errno_t TskReleaseSemaphore (TskSemaphore_t* sem);

// Attempts to lock a semaphore
errno_t TskTryAcquireSemaphore (TskSemaphore_t* sem);

// Closes a semaphore
errno_t TskCloseSemaphore (TskSemaphore_t* sem);

// Mutex
typedef struct _mutex
{
    TskWaitQueue_t queue;    // Queue of threads waiting on mutex
    bool state;              // State of mutex
} TskMutex_t;

// Initializes a mutex
void TskInitMutex (TskMutex_t* mtx);

// Acquires a mutex
errno_t TskAcquireMutex (TskMutex_t* mtx);

// Releases a mutex
errno_t TskReleaseMutex (TskMutex_t* mtx);

// Tries to acquire a mutex
errno_t TskTryAcquireMutex (TskMutex_t* mtx);

// Closes a mutex
errno_t TskCloseMutex (TskMutex_t* mtx);

// Condition
typedef struct _cond
{
    TskWaitQueue_t queue;    // Queue of threads waiting on condtion
    bool state;              // State of condition
} TskCondition_t;

// Initializes a condition
void TskInitCondition (TskCondition_t* cond);

// Waits on a condition, taking a mutex to unlock before blocking
// to prevent lost-wakeup
errno_t TskWaitCondition (TskCondition_t* cond, TskMutex_t* mtx);

// Signals a thread to wake up on condition
errno_t TskSignalCondition (TskCondition_t* cond);

// Broadcasts a condition
errno_t TskBroadcastCondition (TskCondition_t* cond);

// Unsets a condition
void TskUnsetCondition (TskCondition_t* cond);

// Closes a condition
errno_t TskCloseCondition (TskCondition_t* cond);

#endif

/*
    lock.h - contains spinlock manipulators
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

#ifndef _LOCK_H
#define _LOCK_H

#include <nexke/cpu.h>

static inline void TskDisablePreempt();
static inline void TskEnablePreempt();

// Locks a spinlock
// On UP, disables preemption
static FORCEINLINE void NkSpinLock (spinlock_t* lock)
{
    // Disable preemption first
    TskDisablePreempt();
#ifndef NEXKE_UP
    while (__sync_lock_test_and_set (lock, 1))
        while (*lock)
            CpuSpin();
#endif
}

// Unlocks a spinlock
static FORCEINLINE void NkSpinUnlock (spinlock_t* lock)
{
#ifndef NEXKE_UP
    assert (*lock == 1);
    __sync_lock_release (lock);    // Unlock it
#endif
    TskEnablePreempt();    // Re-enable preemption
}

typedef long atomic_t;    // Speical atomic type

// Loads a value atomically
static FORCEINLINE atomic_t NkAtomicLoad (atomic_t* ptr)
{
#ifndef NEXKE_UP
    return __atomic_load_n (ptr, __ATOMIC_SEQ_CST);
#else
    return *ptr;
#endif
}

// Store a value atomically
static FORCEINLINE void NkAtomicStore (atomic_t* ptr, atomic_t val)
{
#ifndef NEXKE_UP
    __atomic_store_n (ptr, val, __ATOMIC_SEQ_CST);
#else
    *ptr = val;
#endif
}

// Adds atomically
static FORCEINLINE atomic_t NkAtomicAdd (atomic_t* ptr, atomic_t val)
{
#ifndef NEXKE_UP
    return __atomic_add_fetch (ptr, val, __ATOMIC_SEQ_CST);
#else
    *ptr += val;
    return *ptr;
#endif
}

// Subtracts atomically
static FORCEINLINE atomic_t NkAtomicSub (atomic_t* ptr, atomic_t val)
{
#ifndef NEXKE_UP
    return __atomic_sub_fetch (ptr, val, __ATOMIC_SEQ_CST);
#else
    *ptr -= val;
    return *ptr;
#endif
}

#endif

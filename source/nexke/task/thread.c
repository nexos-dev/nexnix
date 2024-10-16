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

// Creates a new thread object
NkThread_t* TskCreateThread (NkThreadEntry entry, void* arg, const char* name)
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
    // Initialize CPU specific context
    thread->context = CpuAllocContext ((uintptr_t) entry);
    if (!thread->context)
    {
        // Failure
        NkFreeResource (nkThreadRes, tid);
        MmCacheFree (nkThreadCache, thread);
        return NULL;
    }
    // Add to table
    nkThreadTable[tid] = thread;
    return thread;
}

// Destroys a thread object
void TskDestroyThread (NkThread_t* thread)
{
    // Destory memory structures
    CpuDestroyContext (thread->context);
    NkFreeResource (nkThreadRes, thread->tid);
    MmCacheFree (nkThreadCache, thread);
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
}

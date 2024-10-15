/*
    malloc.c - contains general malloc function
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

#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <stdlib.h>

// This is basically a power-of-two allocator
// It has a slab cache for powers of two from 16 - 8192
// Even though power-of-two allocators aren't that great, they are simple
// and this allocator isn't going to be used very much

#define NUM_POWERS 10

#define CACHE_SZ16   0
#define CACHE_SZ32   1
#define CACHE_SZ64   2
#define CACHE_SZ128  3
#define CACHE_SZ256  4
#define CACHE_SZ512  5
#define CACHE_SZ1024 6
#define CACHE_SZ2048 7
#define CACHE_SZ4096 8
#define CACHE_SZ8192 9

// Array of slab caches
static SlabCache_t* caches[NUM_POWERS] = {NULL};

// Initializes general purpose memory allocator
void MmMallocInit()
{
    int curNum = 16;
    for (int i = 0; i < NUM_POWERS; ++i)
    {
        caches[i] = MmCacheCreate (curNum, "malloc bucket", 0, 0);
        curNum *= 2;
    }
}

static inline SlabCache_t* mallocGetCache (size_t sz)
{
    SlabCache_t* cache = NULL;
    if (sz <= 16)
        cache = caches[CACHE_SZ16];
    else if (sz <= 32)
        cache = caches[CACHE_SZ32];
    else if (sz <= 64)
        cache = caches[CACHE_SZ64];
    else if (sz <= 128)
        cache = caches[CACHE_SZ128];
    else if (sz <= 256)
        cache = caches[CACHE_SZ256];
    else if (sz <= 512)
        cache = caches[CACHE_SZ512];
    else if (sz <= 1024)
        cache = caches[CACHE_SZ1024];
    else if (sz <= 2048)
        cache = caches[CACHE_SZ2048];
    else if (sz <= 4096)
        cache = caches[CACHE_SZ4096];
    else if (sz <= 8192)
        cache = caches[CACHE_SZ8192];
    else
        NkPanic ("nexke: invalid size of %u to malloc", sz);
    return cache;
}

void* kmalloc (size_t sz)
{
    // Figure out power of two we should use
    SlabCache_t* cache = mallocGetCache (sz);
    return MmCacheAlloc (cache);
}

void kfree (void* ptr, size_t sz)
{
    // Figure out power of two we should use
    SlabCache_t* cache = mallocGetCache (sz);
    return MmCacheFree (cache, ptr);
}

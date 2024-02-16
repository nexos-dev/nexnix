/*
    slab.c - contains slab allocator
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
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <string.h>

// Is the PMM available yet?
static bool isPmmInit = false;

// Boot memory pool base
static void* bootPoolBase = NULL;

// Boot pool end
static void* bootPoolEnd = 0;

// Current marker for boot pool
static void* bootPoolMark = NULL;

// Cache of caches
static SlabCache_t caches = {0};

// Alignment value
#define SLAB_ALIGN 8

// Slab states
#define SLAB_STATE_EMPTY   0
#define SLAB_STATE_PARTIAL 1
#define SLAB_STATE_FULL    2

// Max number of empty slabs
// TODO: this should be based on object size
#define SLAB_EMPTY_MAX 3

// Slab structure
typedef struct _slab
{
    MmKvPage_t* page;      // Page underlying this slab
    SlabCache_t* cache;    // Parent cache
    void* slabEnd;         // End of slab
    size_t sz;             // Size of one object
    size_t numAvail;       // Number of available objects
    size_t maxObj;         // Max objects in slab
    void* freeList;        // Pointer to first free object
    size_t numFreed;       // Number of freed objects
    void* allocMark;       // If freeList is empty, this is a hint to where free memory could be
    int state;             // State of slab
    struct _slab* next;    // Next slab in list
    struct _slab* prev;    // Last slab in list
} Slab_t;

// Rounds number up to next multiple of 8
static inline size_t slabRoundTo8 (size_t sz)
{
    // Check if sz is aligned
    if ((sz % SLAB_ALIGN) == 0)
        return sz;
    // Align it
    sz &= ~(SLAB_ALIGN - 1);
    sz += SLAB_ALIGN;
    return sz;
}

// Converts object to slab
// This takes advantage of the fact that an object is always inside a slab,
// and a slab is always page aligned, so if we page align down, then we
// end up with the slab structure
static inline Slab_t* slabGetObjSlab (void* obj)
{
    uintptr_t addr = (uintptr_t) obj;
    addr &= ~(NEXKE_CPU_PAGESZ - 1);
    return (Slab_t*) addr;
}

// Allocates a slab of memory
static Slab_t* slabAllocSlab (SlabCache_t* cache)
{
    // Allocate page
    MmKvPage_t* page = MmAllocKvPage();
    // Initialize slab
    size_t slabSz = slabRoundTo8 (sizeof (Slab_t));
    Slab_t* slab = (Slab_t*) page->vaddr;
    slab->page = page;
    slab->cache = cache;
    slab->allocMark = (void*) page->vaddr + slabRoundTo8 (sizeof (Slab_t));
    slab->slabEnd = (void*) page->vaddr + NEXKE_CPU_PAGESZ;
    slab->freeList = NULL;
    slab->sz = cache->objAlign;    // NOTE: we use the aligned size here
    slab->state =
        SLAB_STATE_PARTIAL;    // Even though slab is empty right now, it won't be for long

    // Set up number of available objects
    slab->numAvail = (NEXKE_CPU_PAGESZ - slabSz) / slab->sz;
    slab->maxObj = slab->numAvail;
    slab->numFreed = 0;

    // Add to list
    if (cache->partialSlabs)
    {
        Slab_t* curFront = cache->partialSlabs;
        curFront->prev = slab;
    }
    slab->next = cache->partialSlabs;
    slab->prev = NULL;
    cache->partialSlabs = slab;
    return slab;
}

// Frees a slab of memory
static void slabFreeSlab (SlabCache_t* cache, Slab_t* slab)
{
    // Remove from cache
    assert (slab->numAvail == slab->maxObj);
    cache->emptySlabs = slab->next;
    if (cache->emptySlabs)
        cache->emptySlabs->prev = NULL;
    --cache->numEmpty;
    // Free frame to allocator
    MmFreeKvPage (slab->page);
}

// Allocates object in specified slab
static inline void* slabAllocInSlab (Slab_t* slab)
{
    assert (slab->numAvail);
    // Check in free list first, as the CPU's cache is more likely to
    // recently freed objects still stored
    void* ret = NULL;
    if (slab->freeList)
    {
        assert (slab->numFreed);
        ret = slab->freeList;
        --slab->numFreed;
        // Update free list
        if (!slab->numFreed)
            slab->freeList = NULL;    // List is now empty
        else
        {
            // Grab next free object
            void** nextFree = ret;
            slab->freeList = *nextFree;
        }
    }
    else
    {
        // Allocate from marker
        ret = slab->allocMark;
        slab->allocMark += slab->sz;
        assert (slab->allocMark <= slab->slabEnd);
    }
    --slab->numAvail;
    return ret;
}

// Frees object to slab
static inline void slabFreeToSlab (Slab_t* slab, void* obj)
{
    ++slab->numFreed;
    ++slab->numAvail;
    // Create pointer
    void** nextPtr = obj;
    *nextPtr = slab->freeList;
    // Put in list
    slab->freeList = nextPtr;
}

// Moves slab to specified list
static inline void slabMoveSlab (SlabCache_t* cache, Slab_t* slab, int newState)
{
    Slab_t** sourceList = NULL;
    Slab_t** destList = NULL;
    // Figure out source
    if (slab->state == SLAB_STATE_EMPTY)
        sourceList = &cache->emptySlabs;
    else if (slab->state == SLAB_STATE_PARTIAL)
        sourceList = &cache->partialSlabs;
    else if (slab->state == SLAB_STATE_FULL)
        sourceList = &cache->fullSlabs;
    // Decide dest
    if (newState == SLAB_STATE_EMPTY)
        destList = &cache->emptySlabs;
    else if (newState == SLAB_STATE_PARTIAL)
        destList = &cache->partialSlabs;
    else if (newState == SLAB_STATE_FULL)
        destList = &cache->fullSlabs;
    // Remove from list
    if (slab->prev)
        slab->prev->next = slab->next;
    if (slab->next)
        slab->next->prev = slab->prev;
    if (slab == *sourceList)
        *sourceList = slab->next;    // Set head if needed
    // Add to head of destList
    if (*destList)
        (*destList)->prev = slab;
    slab->next = *destList;
    slab->prev = NULL;
    *destList = slab;
    slab->state = newState;
}

// Initializes a slab cache
static inline void slabCacheCreate (SlabCache_t* cache,
                                    size_t objSz,
                                    SlabObjConstruct constuctor,
                                    SlabObjDestruct destructor)
{
    cache->constructor = constuctor;
    cache->destructor = destructor;
    cache->objSz = objSz;
    cache->objAlign = slabRoundTo8 (objSz);
    cache->emptySlabs = NULL, cache->partialSlabs = NULL, cache->fullSlabs = NULL;
    cache->numEmpty = 0;
    cache->numObjs = 0;
}

// Allocates an object from a cache
void* MmCacheAlloc (SlabCache_t* cache)
{
    // Attempt to grab object from empty list
    void* ret = NULL;
    if (cache->emptySlabs)
    {
        Slab_t* emptySlab = cache->emptySlabs;
        ret = slabAllocInSlab (emptySlab);
        // Slab is no longer empty, move to partial list
        --cache->numEmpty;
        slabMoveSlab (cache, emptySlab, SLAB_STATE_PARTIAL);
    }
    // Now try partial slab
    else if (cache->partialSlabs)
    {
        Slab_t* slab = cache->partialSlabs;
        ret = slabAllocInSlab (slab);
        // If slab is full, move to full list
        if (slab->numAvail == 0)
            slabMoveSlab (cache, slab, SLAB_STATE_FULL);
    }
    else
    {
        // No memory is available in cache, get more
        Slab_t* newSlab = slabAllocSlab (cache);
        if (!newSlab)
            return NULL;    // OOM
        // Slab is already in partial state and ready to go, allocate an obejct
        ret = slabAllocInSlab (newSlab);
    }
    // Now construct object
    if (cache->constructor)
        cache->constructor (ret);
    // Update stats
    ++cache->numObjs;
    return ret;    // We are done!
}

// Frees an object back to slab cache
void MmCacheFree (SlabCache_t* cache, void* obj)
{
    // Destroy object
    if (cache->destructor)
        cache->destructor (obj);
    // Put object back in parent slab
    Slab_t* slab = slabGetObjSlab (obj);
    slabFreeToSlab (slab, obj);
    // Check if slab is now empty
    if (slab->numAvail == slab->maxObj)
    {
        if (cache->numEmpty >= SLAB_EMPTY_MAX)
            slabFreeSlab (cache, slab);    // Free this slab
        else
        {
            slabMoveSlab (cache, slab, SLAB_STATE_EMPTY);    // Move this slab
            ++cache->numEmpty;
        }
    }
    --cache->numObjs;
}

// Creates a slab cache
SlabCache_t* MmCacheCreate (size_t objSz, SlabObjConstruct constuctor, SlabObjDestruct destructor)
{
    if (objSz >= NEXKE_CPU_PAGESZ)
        return NULL;    // Can't allocate anything larger than that
    // Allocate cache from cache of caches
    SlabCache_t* newCache = MmCacheAlloc (&caches);
    if (!newCache)
        return NULL;
    slabCacheCreate (newCache, objSz, constuctor, destructor);
    return newCache;
}

// Destroys a slab cache
void MmCacheDestroy (SlabCache_t* cache)
{
    // Ensure cache is empty
    if (cache->numObjs)
    {
        // TODO: panic
        assert (0);
    }
    // Release all slabs
    Slab_t* curSlab = cache->fullSlabs;
    while (curSlab)
    {
        Slab_t* slab = curSlab;
        slabFreeSlab (cache, slab);
        curSlab = curSlab->next;
    }
    curSlab = cache->partialSlabs;
    while (curSlab)
    {
        Slab_t* slab = curSlab;
        slabFreeSlab (cache, slab);
        curSlab = curSlab->next;
    }
    curSlab = cache->emptySlabs;
    while (curSlab)
    {
        Slab_t* slab = curSlab;
        slabFreeSlab (cache, slab);
        curSlab = curSlab->next;
    }
    // Free it from cache of caches
    MmCacheFree (&caches, cache);
}

// Returns cache of given pointer
SlabCache_t* MmGetCacheFromPtr (void* ptr)
{
    Slab_t* slab = slabGetObjSlab (ptr);
    return slab->cache;
}

// Bootstraps the slab allocator
void MmSlabBootstrap()
{
    // Initialize cache of caches
    slabCacheCreate (&caches, sizeof (SlabCache_t), NULL, NULL);
}

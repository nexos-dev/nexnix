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

// Cache of caches
static SlabCache_t caches = {0};

// Cache of external slabs
static SlabCache_t extSlabCache = {0};

// Cache of external buffers
static SlabCache_t extBufCache = {0};

// List of caches
static NkList_t cacheList = {0};

// Minimum object size
static size_t minObjSz = 0;

// Hash table of external buffers
#define SLAB_EXT_HASH_SZ 64
static NkList_t extBufHash[SLAB_EXT_HASH_SZ] = {0};
static spinlock_t bufHashLock = 0;

// Alignment value
#define SLAB_ALIGN 8

// Min for ext slabs
#define SLAB_EXT_MIN 1024

// Min number of objects to fit in a slab
#define SLAB_OBJ_MIN 6

// Max number of empty slabs
// TODO: this should be based on object size
#define SLAB_EMPTY_MAX 3

// Slab buffer
typedef struct _slabbuf
{
    void* obj;        // Address of object
    Slab_t* slab;     // Slab object belongs to
    NkLink_t link;    // Link to next buffer
} SlabBuf_t;

// Slab structure
typedef struct _slab
{
    // Management info
    SlabCache_t* cache;
    uintptr_t base;    // Base address
    size_t numAvail;
    // Buffering info
    NkList_t freeList;    // Pointer to first free object
    NkLink_t link;
    NkLink_t hashLink;
} Slab_t;

// Aligns a size
static FORCEINLINE size_t slabAlignSz (size_t sz, size_t align)
{
    // Align it
    sz += align - 1;
    sz &= ~(align - 1);
    return sz;
}

// Aligns downwards
static FORCEINLINE uintptr_t slabAlignDown (uintptr_t ptr, size_t align)
{
    return ptr & ~(align - 1);
}

// Gets a buffer from the hash table
static FORCEINLINE SlabBuf_t* slabGetHashedBuf (void* base)
{
    // Get has table index
    // All bases are 8-byte aligned (generally) removes those bits for uniqueness
    size_t hashIdx = ((uintptr_t) base >> 3) % SLAB_EXT_HASH_SZ;
    NkSpinLock (&bufHashLock);
    NkLink_t* iter = NkListFront (&extBufHash[hashIdx]);
    SlabBuf_t* buf = NULL;
    while (iter)
    {
        buf = LINK_CONTAINER (iter, SlabBuf_t, link);
        if (buf->obj == base)
            break;
        iter = NkListIterate (iter);
    }
    NkSpinUnlock (&bufHashLock);
    return NULL;
}

// Adds a slab to the hash table
static FORCEINLINE void slabHashBuf (SlabBuf_t* buf)
{
    size_t hashIdx = ((uintptr_t) buf->obj >> 3) % SLAB_EXT_HASH_SZ;
    NkSpinLock (&bufHashLock);
    NkListAddFront (&extBufHash[hashIdx], &buf->link);
    NkSpinUnlock (&bufHashLock);
}

// Removes a slab from the hash table
static FORCEINLINE void slabRemoveBuf (SlabBuf_t* buf)
{
    size_t hashIdx = ((uintptr_t) buf->obj >> 3) % SLAB_EXT_HASH_SZ;
    NkSpinLock (&bufHashLock);
    NkListRemove (&extBufHash[hashIdx], &buf->link);
    NkSpinUnlock (&bufHashLock);
}

// Converts object to slab
// This takes advantage of the fact that an object is always inside a slab,
// and a slab is always page aligned, so if we page align down, then we
// end up with the slab structure
// For external slabs we reference the hash table
static FORCEINLINE Slab_t* slabGetObjSlab (SlabCache_t* cache, void* obj)
{
    Slab_t* slab = NULL;
    if (cache->flags & SLAB_CACHE_EXT_SLAB)
    {
        // Convert object into buffer
        SlabBuf_t* buf = slabGetHashedBuf (obj);
        assert (buf);
        assert (buf->obj == obj);
        slab = buf->slab;
    }
    else
    {
        // Round down to get slab base
        uintptr_t addr = (uintptr_t) obj;
        size_t slabSz = cache->slabSz << NEXKE_CPU_PAGE_SHIFT;
        addr = slabAlignDown (addr, slabSz);
        // Get slab structure
        slab = (Slab_t*) (addr + slabSz - sizeof (Slab_t));
    }
    return slab;
}

// Allocates a slab of memory
static Slab_t* slabAllocSlab (SlabCache_t* cache)
{
    // Get slab
    void* ptr = MmAllocKvRegion (cache->slabSz,
                                 (cache->flags & SLAB_CACHE_DEMAND_PAGE) ? 0 : MM_KV_NO_DEMAND);
    if (!ptr)
        return NULL;
    // Find slab structure. For internal caches, this is at the end of the slab.
    // Otherwise we have to allocate it
    Slab_t* slab = NULL;
    if (cache->flags & SLAB_CACHE_EXT_SLAB)
    {
        // Get control structure from cache
        slab = MmCacheAlloc (&extSlabCache);
        if (!slab)
        {
            MmFreeKvRegion (ptr);
            return NULL;
        }
    }
    else
        slab = (Slab_t*) (ptr + (cache->slabSz << NEXKE_CPU_PAGE_SHIFT) - sizeof (Slab_t));
    // Color the slab
    ptr += cache->curColor;
    // Adjust the color
    cache->curColor += cache->colorAdj;
    if (cache->curColor > cache->numColors)
        cache->curColor = 0;
    // Set up slab
    NkListInit (&slab->freeList);
    slab->numAvail = cache->maxObj;
    slab->base = (uintptr_t) ptr;
    slab->cache = cache;
    // Set up list of free objects
    for (int i = 0; i < slab->numAvail; ++i)
    {
        SlabBuf_t* cur = NULL;
        void* curObj = ptr + (cache->objSz * i);
        if (cache->flags & SLAB_CACHE_EXT_SLAB)
            cur = MmCacheAlloc (&extBufCache);
        else
            cur = curObj;    // Buffers are stored with objects
        cur->obj = curObj;
        cur->slab = slab;
        cur->link.next = NULL, cur->link.prev = NULL;
        NkListAddBack (&slab->freeList, &cur->link);
    }
    // Add to list
    NkListAddFront (&cache->partialSlabs, &slab->link);
    ++cache->numPartial;
    return slab;
}

// Frees a slab of memory
static void slabFreeSlab (SlabCache_t* cache, Slab_t* slab)
{
    // Remove from cache
    assert (slab->numAvail == cache->maxObj);
    NkListRemove (&cache->emptySlabs, &slab->link);
    --cache->numEmpty;
    // Free frame
    MmFreeKvRegion ((void*) slab->base);
}

// Allocates object in specified slab
static FORCEINLINE void* slabAllocInSlab (SlabCache_t* cache, Slab_t* slab)
{
    assert (slab->numAvail);
    // Grab first buffer
    NkLink_t* link = NkListPopFront (&slab->freeList);
    SlabBuf_t* buf = LINK_CONTAINER (link, SlabBuf_t, link);
    void* obj = buf->obj;
    --slab->numAvail;
    // Determine if we need to hash it
    if (cache->flags & SLAB_CACHE_EXT_SLAB)
        slabHashBuf (buf);
    return obj;
}

// Frees object to slab
static FORCEINLINE void slabFreeToSlab (SlabCache_t* cache, Slab_t* slab, void* obj)
{
    // Create new buffer
    SlabBuf_t* buf = NULL;
    if (cache->flags & SLAB_CACHE_EXT_SLAB)
    {
        buf = slabGetHashedBuf (obj);
        slabRemoveBuf (buf);
    }
    else
        buf = (SlabBuf_t*) obj;
    buf->obj = obj;
    buf->slab = slab;
    NkListAddFront (&slab->freeList, &buf->link);
    ++slab->numAvail;
}

// Initializes a slab cache
static FORCEINLINE void slabCacheCreate (SlabCache_t* cache,
                                         size_t objSz,
                                         const char* name,
                                         size_t align,
                                         int flags)
{
    cache->name = name;
    cache->align = (align) ? align : SLAB_ALIGN;
    size_t sz = (objSz < minObjSz) ? minObjSz : objSz;
    cache->objSz = slabAlignSz (sz, cache->align);
    // Unset private flags
    flags &= ~(SLAB_CACHE_EXT_SLAB);
    cache->flags = flags;
    // Determine size of one slab in pages
    cache->slabSz = CpuPageAlignUp (objSz * SLAB_OBJ_MIN) >> NEXKE_CPU_PAGE_SHIFT;
    // Figure out whether we should be internal or external
    // For one page slabs, internal, anything more, external
    if (cache->slabSz > 1)
        cache->flags |= SLAB_CACHE_EXT_SLAB;
    // Set up lists
    NkListInit (&cache->partialSlabs);
    NkListInit (&cache->fullSlabs);
    NkListInit (&cache->emptySlabs);
    // Initialize stats
    cache->numEmpty = 0, cache->numFull = 0, cache->numPartial = 0;
    cache->numObjs = 0;
    // Determine max number of objects
    if (cache->flags & SLAB_CACHE_EXT_SLAB)
        cache->maxObj = (cache->slabSz << NEXKE_CPU_PAGE_SHIFT) / cache->objSz;
    else
        cache->maxObj = ((cache->slabSz << NEXKE_CPU_PAGE_SHIFT) - sizeof (Slab_t)) / cache->objSz;
    // Figure out coloring info
    size_t slabSz = cache->slabSz << NEXKE_CPU_PAGE_SHIFT;
    if (!(cache->flags & SLAB_CACHE_EXT_SLAB))
        slabSz -= sizeof (Slab_t);
    size_t waste = slabSz % cache->objSz;
    // Set up coloring info
    cache->curColor = 0;
    cache->colorAdj = cache->align;
    cache->numColors = slabAlignDown (waste, cache->align);
    // Add to list
    NkListAddBack (&cacheList, &cache->link);
}

// Allocates an object from a cache
void* MmCacheAlloc (SlabCache_t* cache)
{
    CPU_ASSERT_NOT_INT();
    NkSpinLock (&cache->lock);
    // Attempt to grab object from empty list
    void* ret = NULL;
    if (NkListFront (&cache->emptySlabs))
    {
        Slab_t* emptySlab = LINK_CONTAINER (NkListFront (&cache->emptySlabs), Slab_t, link);
        ret = slabAllocInSlab (cache, emptySlab);
        // Slab is no longer empty, move to partial list
        NkListRemove (&cache->emptySlabs, &emptySlab->link);
        --cache->numEmpty;
        NkListAddFront (&cache->partialSlabs, &emptySlab->link);
        ++cache->numPartial;
    }
    // Now try partial slab
    else if (NkListFront (&cache->partialSlabs))
    {
        Slab_t* slab = LINK_CONTAINER (NkListFront (&cache->partialSlabs), Slab_t, link);
        ret = slabAllocInSlab (cache, slab);
        // If slab is full, move to full list
        if (slab->numAvail == 0)
        {
            NkListRemove (&cache->partialSlabs, &slab->link);
            --cache->numPartial;
            NkListAddFront (&cache->fullSlabs, &slab->link);
            ++cache->numFull;
        }
    }
    else
    {
        // No memory is available in cache, get more
        Slab_t* newSlab = slabAllocSlab (cache);
        if (!newSlab)
            return NULL;    // OOM
        // Slab is already in partial state and ready to go, allocate an obejct
        ret = slabAllocInSlab (cache, newSlab);
    }
    // Update stats
    ++cache->numObjs;
    NkSpinUnlock (&cache->lock);
    return ret;    // We are done!
}

// Frees an object back to slab cache
void MmCacheFree (SlabCache_t* cache, void* obj)
{
    CPU_ASSERT_NOT_INT();
    NkSpinLock (&cache->lock);
    // Put object back in parent slab
    Slab_t* slab = slabGetObjSlab (cache, obj);
    slabFreeToSlab (cache, slab, obj);
    // See if it's gone from full to empty
    if (slab->numAvail == 1)
    {
        NkListRemove (&cache->fullSlabs, &slab->link);
        --cache->numFull;
        // Add to back since it will probably be added back to full list again
        // Which could ultimatly cause thrashing if we continually allocate and free
        // the same object
        NkListAddBack (&cache->partialSlabs, &slab->link);
    }
    // Check if slab is now empty
    else if (slab->numAvail == cache->maxObj)
    {
        NkListRemove (&cache->partialSlabs, &slab->link);
        --cache->numPartial;
        NkListAddFront (&cache->emptySlabs, &slab->link);
        ++cache->numEmpty;
        if (cache->numEmpty >= SLAB_EMPTY_MAX)
            slabFreeSlab (cache, slab);    // Free this slab
    }
    --cache->numObjs;
    NkSpinUnlock (&cache->lock);
}

// Creates a slab cache
SlabCache_t* MmCacheCreate (size_t objSz, const char* name, size_t align, int flags)
{
    CPU_ASSERT_NOT_INT();
    // Allocate cache from cache of caches
    SlabCache_t* newCache = MmCacheAlloc (&caches);
    if (!newCache)
        return NULL;
    memset (newCache, 0, sizeof (SlabCache_t));
    slabCacheCreate (newCache, objSz, name, align, flags);
    return newCache;
}

// Destroys a slab cache
void MmCacheDestroy (SlabCache_t* cache)
{
    CPU_ASSERT_NOT_INT();
    // Ensure cache is empty
    if (cache->numObjs)
        NkPanic ("nexke: panic: attempt to destroy non-empty cache\n");
    // Release all slabs
    NkLink_t* iter = NkListFront (&cache->fullSlabs);
    while (iter)
    {
        Slab_t* slab = LINK_CONTAINER (iter, Slab_t, link);
        slabFreeSlab (cache, slab);
        iter = NkListIterate (iter);
    }
    iter = NkListFront (&cache->partialSlabs);
    while (iter)
    {
        Slab_t* slab = LINK_CONTAINER (iter, Slab_t, link);
        slabFreeSlab (cache, slab);
        iter = NkListIterate (iter);
    }
    iter = NkListFront (&cache->emptySlabs);
    while (iter)
    {
        Slab_t* slab = LINK_CONTAINER (iter, Slab_t, link);
        slabFreeSlab (cache, slab);
        iter = NkListIterate (iter);
    }
    // Remove from list
    NkListRemove (&cacheList, &cache->link);
    // Free it from cache of caches
    MmCacheFree (&caches, cache);
}

// Bootstraps the slab allocator
void MmSlabBootstrap()
{
    // Set globals
    minObjSz = sizeof (SlabBuf_t);
    // Initialize cache of caches
    slabCacheCreate (&caches, sizeof (SlabCache_t), "SlabCache_t", 0, 0);
    // Initialize cache of slabs
    slabCacheCreate (&extSlabCache, sizeof (Slab_t), "Slab_t", 0, 0);
    // Initialize caches of buffers
    slabCacheCreate (&extBufCache, sizeof (SlabBuf_t), "SlabBuf_t", 0, 0);
}

// Dumps the state of the slab allocator
void MmSlabDump()
{
    NkLink_t* cacheIter = NkListFront (&cacheList);
    while (cacheIter)
    {
        SlabCache_t* cache = LINK_CONTAINER (cacheIter, SlabCache_t, link);
        NkSpinLock (&cache->lock);
        NkLogDebug ("cache name: %s, cache object size: %lu, cache aligment: %lu, max number of "
                    "objects to a slab: %lu\n",
                    cache->name,
                    cache->objSz,
                    cache->align,
                    cache->maxObj);
        NkLogDebug ("Number empty slabs: %d, number full slabs: %d, number partial slabs: "
                    "%d, number of objects: %d, number of pages per slab: %lu\n",
                    cache->numEmpty,
                    cache->numFull,
                    cache->numPartial,
                    cache->numObjs,
                    cache->slabSz);
        NkLogDebug ("Number of colors: %lu, current color: %lu, color adjust: %lu\n\n",
                    cache->numColors,
                    cache->curColor,
                    cache->colorAdj);
        NkSpinUnlock (&cache->lock);
        cacheIter = NkListIterate (cacheIter);
    }
}

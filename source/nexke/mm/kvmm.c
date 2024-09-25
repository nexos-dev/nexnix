/*
    kvmm.c - contains kernel virtual memory manager
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

#include "backends.h"
#include <assert.h>
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <string.h>

#define MM_KV_MAX_FREELIST 16
#define MM_KV_MAX_BUCKETS  5

// Kernel virtual region structure
typedef struct _kvregion
{
    uintptr_t vaddr;    // Virtual address of page
    size_t numPages;    // Number of pages in region
    bool isFree;        // Is this region free?
    struct _kvregion* next;
    struct _kvregion* prev;
} MmKvRegion_t;

// Kernel virtual region footer
typedef struct _kvfooter
{
    size_t magic;       // 0xDEADBEEF
    size_t regionSz;    // Size of this region
} MmKvFooter_t;

// Kernel memory bucket
typedef struct _kvbucket
{
    MmKvRegion_t* regionList;    // List of regions in bucket
    size_t bucketNum;            // The bucket number of this bucket
} MmKvBucket_t;

// Kernel free arena
typedef struct _kvarena
{
    MmKvBucket_t buckets[MM_KV_MAX_BUCKETS];    // Memory buckets
    size_t numPages;                            // Number of pages in arena
    size_t numFreePages;
    bool needsMap;    // Whether this arena is pre-mapped

    MmKvRegion_t* freeList;    // Free list of pages
    size_t freeListSz;         // Number of pages in free list currently

    uintptr_t resvdStart;    // Start of reserved area
    size_t resvdSz;          // Size of reserved area in pages

    uintptr_t start;    // Bounds of arena
    uintptr_t end;
    struct _kvarena* next;    // Next arena
} MmKvArena_t;

#define MM_KV_FOOTER_MAGIC 0xDEADBEEF

typedef struct _kvregion MmKvPage_t;

// Bucket sizes
#define MM_BUCKET_1TO4   0
#define MM_BUCKET_5TO8   1
#define MM_BUCKET_9TO16  2
#define MM_BUCKET_17TO32 3
#define MM_BUCKET_32PLUS 4

// Arena
static MmKvArena_t* mmArenas = NULL;

static MmSpace_t kmemSpace = {0};

// Boot pool globals
static uintptr_t bootPoolBase = 0;
static uintptr_t bootPoolMark = 0;
static uintptr_t bootPoolEnd = 0;
static size_t bootPoolSz = NEXBOOT_MEMPOOL_SZ;
static bool mmInit = false;    // Wheter normal MM is up yet

// Adds arena to list
static void mmKvAddArena (MmKvArena_t* arena)
{
    if (!mmArenas)
    {
        mmArenas = arena;
        arena->next = NULL;
    }
    else
    {
        arena->next = mmArenas;
        mmArenas = arena;
    }
}

// Gets arena from pointer
static MmKvArena_t* mmKvGetArena (void* ptr)
{
    MmKvArena_t* curArena = mmArenas;
    while (curArena)
    {
        if (ptr >= (void*) curArena->start && ptr <= (void*) curArena->end)
            return curArena;
        curArena = curArena->next;
    }
    assert (false);
}

// Gets bucket number from size
static int mmKvGetBucket (size_t sz)
{
    assert (sz);
    if (sz <= 4)
        return MM_BUCKET_1TO4;
    else if (sz <= 8)
        return MM_BUCKET_5TO8;
    else if (sz <= 16)
        return MM_BUCKET_9TO16;
    else if (sz <= 32)
        return MM_BUCKET_17TO32;
    else
        return MM_BUCKET_32PLUS;
}

// Gets region structure from address
static MmKvRegion_t* mmKvGetRegion (MmKvArena_t* arena, uintptr_t addr)
{
    // Get offset in page count
    pfn_t pageOffset =
        (addr - (arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ))) / NEXKE_CPU_PAGESZ;
    // Map into reserved area
    return (MmKvRegion_t*) arena->resvdStart + pageOffset;
}

// Gets region footer
static MmKvFooter_t* mmKvGetRegionFooter (MmKvArena_t* arena, uintptr_t base, size_t sz)
{
    // Get offset in page count
    pfn_t pageOffset =
        ((base - (arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ))) / NEXKE_CPU_PAGESZ) +
        (sz - 1);
    // Map into reserved area
    return (MmKvFooter_t*) (arena->resvdStart + (pageOffset * sizeof (MmKvRegion_t)));
}

// Initializes boot pool
void MmInitKvm1()
{
    // Get boot info
    NexNixBoot_t* bootInfo = NkGetBootArgs();
    // Initialize boot pool
    bootPoolBase = (uintptr_t) bootInfo->memPool;
    bootPoolMark = bootPoolBase;
    bootPoolEnd = bootPoolBase + bootInfo->memPoolSize;
    // Figure out how many pages we need to reserve
    MmKvArena_t* arena = (MmKvArena_t*) bootPoolBase;
    memset (arena, 0, sizeof (MmKvArena_t));
    arena->needsMap = false;
    arena->resvdStart = bootPoolBase + sizeof (MmKvArena_t);
    arena->resvdSz =
        CpuPageAlignUp ((((bootInfo->memPoolSize / NEXKE_CPU_PAGESZ) * sizeof (MmKvRegion_t)) +
                         sizeof (MmKvArena_t))) /
        NEXKE_CPU_PAGESZ;
    arena->start = bootPoolBase;
    arena->end = bootPoolEnd;
    arena->numPages = (bootInfo->memPoolSize / NEXKE_CPU_PAGESZ) - arena->resvdSz;
    arena->numFreePages = arena->numPages;
    // Initialize buckets
    for (int i = 0; i < MM_KV_MAX_BUCKETS; ++i)
        arena->buckets[i].bucketNum = i;
    // Create a region for the entire arena
    MmKvRegion_t* firstRegion =
        mmKvGetRegion (arena, arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ));
    firstRegion->next = firstRegion->prev = NULL;
    firstRegion->numPages = arena->numPages;
    firstRegion->vaddr = arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ);
    // Create footer
    MmKvFooter_t* footer = mmKvGetRegionFooter (arena,
                                                arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ),
                                                firstRegion->numPages);
    footer->magic = MM_KV_FOOTER_MAGIC;
    footer->regionSz = firstRegion->numPages;
    // Add to bucket
    arena->buckets[MM_BUCKET_32PLUS].regionList = firstRegion;
    // Add to arena list
    mmKvAddArena (arena);
}

// Second phase KVM init
void MmInitKvm2()
{
    // Setup kernel MM space
    kmemSpace.startAddr = NEXKE_KERNEL_ADDR_START;
    kmemSpace.endAddr = NEXKE_KERNEL_ADDR_END;
    size_t numPages = ((NEXKE_KERNEL_ADDR_END + 1) - NEXKE_KERNEL_ADDR_START) / NEXKE_CPU_PAGESZ;
    // Allocate object
    MmObject_t* object =
        MmCreateObject (numPages, MM_BACKEND_KERNEL, MUL_PAGE_R | MUL_PAGE_KE | MUL_PAGE_RW);
    assert (object);
    kmemSpace.entryList = MmAllocSpace (&kmemSpace, object, NEXKE_KERNEL_ADDR_START, numPages);
    // Create new arena
    MmKvArena_t* arena = (MmKvArena_t*) kmemSpace.startAddr;
    memset (arena, 0, sizeof (MmKvArena_t));
    arena->needsMap = true;
    arena->resvdStart = kmemSpace.startAddr + sizeof (MmKvArena_t);
    arena->resvdSz =
        CpuPageAlignUp (((((kmemSpace.endAddr - kmemSpace.startAddr) / NEXKE_CPU_PAGESZ) *
                          sizeof (MmKvRegion_t)) +
                         sizeof (MmKvArena_t))) /
        NEXKE_CPU_PAGESZ;
    arena->start = kmemSpace.startAddr;
    arena->end = kmemSpace.endAddr;
    arena->numPages =
        ((kmemSpace.endAddr - kmemSpace.startAddr) / NEXKE_CPU_PAGESZ) - arena->resvdSz;
    arena->numFreePages = arena->numPages;
    // Initialize buckets
    for (int i = 0; i < MM_KV_MAX_BUCKETS; ++i)
        arena->buckets[i].bucketNum = i;
    // Create a region for the entire arena
    MmKvRegion_t* firstRegion =
        mmKvGetRegion (arena, kmemSpace.startAddr + (arena->resvdSz * NEXKE_CPU_PAGESZ));
    firstRegion->next = firstRegion->prev = NULL;
    firstRegion->numPages = arena->numPages;
    firstRegion->vaddr = arena->start + (arena->resvdSz * NEXKE_CPU_PAGESZ);
    // Create footer
    MmKvFooter_t* footer =
        mmKvGetRegionFooter (arena,
                             kmemSpace.startAddr + (arena->resvdSz * NEXKE_CPU_PAGESZ),
                             firstRegion->numPages);
    footer->magic = MM_KV_FOOTER_MAGIC;
    footer->regionSz = firstRegion->numPages;
    // Add to bucket
    arena->buckets[MM_BUCKET_32PLUS].regionList = firstRegion;
    mmKvAddArena (arena);
}

// Adds memory to a bucket
static void mmKvAddToBucket (MmKvBucket_t* bucket, MmKvRegion_t* region)
{
    region->next = bucket->regionList;
    region->prev = NULL;
    if (bucket->regionList)
        bucket->regionList->prev = region;
    bucket->regionList = region;
}

// Removes memory from bucket
static void mmKvRemoveFromBucket (MmKvBucket_t* bucket, MmKvRegion_t* region)
{
    if (region->next)
        region->next->prev = region->prev;
    if (region->prev)
        region->prev->next = region->next;
    if (region == bucket->regionList)
        bucket->regionList = region->next;
}

// Allocates memory in arena
static void* mmAllocKvInArena (MmKvArena_t* arena, size_t numPages)
{
    // Figure out which bucket we should look in
    int bucketIdx = mmKvGetBucket (numPages);
    MmKvBucket_t* bucket = &arena->buckets[bucketIdx];
    MmKvRegion_t* foundRegion = NULL;
    MmKvRegion_t* curRegion = bucket->regionList;
    while (!foundRegion)
    {
        while (curRegion)
        {
            if (curRegion->numPages >= numPages)
            {
                foundRegion = curRegion;
                break;
            }
            curRegion = curRegion->next;
        }
        // Did we find a region
        if (foundRegion)
            break;
        // Move to another bucket
        // Check if we have any more buckets to check
        if (bucketIdx == MM_BUCKET_32PLUS)
            return NULL;
        bucket = &arena->buckets[++bucketIdx];
        curRegion = bucket->regionList;
    }
    assert (foundRegion);
    // We have now found a memory region
    // Decrease free size
    arena->numFreePages -= numPages;
    // Check if this is a perfect fit
    if (foundRegion->numPages == numPages)
    {
        // Remove from bucket and return
        mmKvRemoveFromBucket (&arena->buckets[bucketIdx], foundRegion);
        foundRegion->isFree = false;
    }
    else
    {
        // Get size of block we are splitting off
        size_t splitSz = foundRegion->numPages - numPages;
        foundRegion->numPages = numPages;
        foundRegion->isFree = false;
        // Now get the region structure
        uintptr_t splitRegionBase = foundRegion->vaddr + (foundRegion->numPages * NEXKE_CPU_PAGESZ);
        MmKvRegion_t* splitRegion = mmKvGetRegion (arena, splitRegionBase);
        splitRegion->isFree = true;
        splitRegion->numPages = splitSz;
        splitRegion->vaddr = splitRegionBase;
        // Figure out which bucket it goes in
        int bucketIdx = mmKvGetBucket (splitRegion->numPages);
        // Add it
        mmKvAddToBucket (&arena->buckets[bucketIdx], splitRegion);
        // Update footers if they are needed
        if (numPages > 1)
        {
            MmKvFooter_t* footerLeft = mmKvGetRegionFooter (arena, foundRegion->vaddr, numPages);
            footerLeft->magic = MM_KV_FOOTER_MAGIC;
            footerLeft->regionSz = numPages;
        }
        if (splitSz > 1)
        {
            MmKvFooter_t* footerRight = mmKvGetRegionFooter (arena, splitRegionBase, splitSz);
            footerRight->magic = MM_KV_FOOTER_MAGIC;
            footerRight->regionSz = splitSz;
        }
        // Remove found region from bucket
        mmKvRemoveFromBucket (&arena->buckets[bucketIdx], foundRegion);
    }
    return (void*) foundRegion->vaddr;
}

// Brings memory in for region
static void mmKvGetMemory (void* p, size_t numPages)
{
    // Get base offset
    uintptr_t offset = (uintptr_t) p - kmemSpace.startAddr;
    MmObject_t* kmemObj = kmemSpace.entryList->obj;
    for (int i = 0; i < numPages; ++i)
    {
        MmPage_t* pg = MmAllocPage();
        if (!pg)
            NkPanicOom();
        MmAddPage (&kmemObj->pageList, pg, offset + (i * NEXKE_CPU_PAGESZ));
        MmMulMapPage (&kmemSpace,
                      (uintptr_t) p + (i * NEXKE_CPU_PAGESZ),
                      pg,
                      MUL_PAGE_KE | MUL_PAGE_RW | MUL_PAGE_R);
        MmBackendPageIn (kmemObj, offset + (i * NEXKE_CPU_PAGESZ));
    }
}

// Frees memory for page
static void mmKvFreeMemory (void* p, size_t numPages)
{
    assert (kmemSpace.startAddr == kmemSpace.entryList->vaddr);
    size_t offset = (size_t) p - kmemSpace.startAddr;
    MmObject_t* kmemObj = kmemSpace.entryList->obj;
    for (int i = 0; i < numPages; ++i)
    {
        MmPage_t* page = MmLookupPage (&kmemObj->pageList, offset);
        if (page)
        {
            // Free it
            MmPageClearMaps (page);
            MmRemovePage (&kmemObj->pageList, page);
            MmFreePage (page);
        }
        offset += NEXKE_CPU_PAGESZ;
    }
}

// Allocates a region of memory
void* MmAllocKvRegion (size_t numPages, int flags)
{
    // Find arena that has enough free pages
    MmKvArena_t* arena = mmArenas;
    while (arena)
    {
        if (arena->numFreePages >= numPages)
        {
            void* p = mmAllocKvInArena (arena, numPages);
            if (p)
            {
                if (!(flags & MM_KV_NO_DEMAND) && arena->needsMap)
                {
                    // Go ahead and bring in pages for this memory region
                    mmKvGetMemory (p, numPages);
                }
                return p;
            }
        }
        arena = arena->next;
    }
    return NULL;
}

// Frees a region of memory
void MmFreeKvRegion (void* mem)
{
    // Get which arena memory is in
    MmKvArena_t* arena = mmKvGetArena (mem);
    // Get region header
    MmKvRegion_t* region = mmKvGetRegion (arena, (uintptr_t) mem);
    size_t numPages = region->numPages;
    region->isFree = true;
    arena->numFreePages += region->numPages;
    // Check if we need to join this block with any adjacent blocks
    // Check if we have a block to the left
    MmKvFooter_t* leftFooter = (MmKvFooter_t*) (region - 1);
    if (region != (void*) arena->resvdStart && leftFooter->magic == MM_KV_FOOTER_MAGIC)
    {
        // Get region
        MmKvRegion_t* leftRegion =
            mmKvGetRegion (arena, (uintptr_t) mem - (leftFooter->regionSz * NEXKE_CPU_PAGESZ));
        if (leftRegion->isFree)
        {
            mmKvRemoveFromBucket (&arena->buckets[mmKvGetBucket (leftRegion->numPages)],
                                  leftRegion);
            leftRegion->numPages += region->numPages;
            // Update footer
            MmKvFooter_t* newFooter =
                mmKvGetRegionFooter (arena, leftRegion->vaddr, leftRegion->numPages);
            newFooter->magic = MM_KV_FOOTER_MAGIC;
            newFooter->regionSz = leftRegion->numPages;
            // This region absorbed the other one so make it the one we work on from now on
            region = leftRegion;
        }
    }
    // Check if we have a free block to the right
    MmKvRegion_t* nextRegion =
        (MmKvRegion_t*) mmKvGetRegionFooter (arena, region->vaddr, region->numPages) + 1;
    // Check if its free and valid
    if (nextRegion->vaddr == region->vaddr + (region->numPages * NEXKE_CPU_PAGESZ) &&
        nextRegion->isFree)
    {
        // Remove from bucket
        mmKvRemoveFromBucket (&arena->buckets[mmKvGetBucket (region->numPages)], nextRegion);
        region->numPages += nextRegion->numPages;
        MmKvFooter_t* newFooter = mmKvGetRegionFooter (arena, region->vaddr, region->numPages);
        newFooter->magic = MM_KV_FOOTER_MAGIC;
        newFooter->regionSz = region->numPages;
    }
    // Add region to appropriate bucket
    mmKvAddToBucket (&arena->buckets[mmKvGetBucket (region->numPages)], region);
    // Unmap and free memory
    if (arena->needsMap)
        mmKvFreeMemory (mem, numPages);
}

// Allocates a memory page for kernel
void* MmAllocKvPage()
{
    return MmAllocKvRegion (1, MM_KV_NO_DEMAND);
}

// Frees a memory page for kernel
void MmFreeKvPage (void* page)
{
    MmFreeKvRegion (page);
}

// Returns kernel address space
MmSpace_t* MmGetKernelSpace()
{
    return &kmemSpace;
}

// Maps in MMIO / FW memory
void* MmAllocKvMmio (void* phys, int numPages, int perm)
{
    // Allocate number of virtual pages for this memory
    void* virt = MmAllocKvRegion (numPages, 0);
    if (!virt)
        NkPanicOom();
    uintptr_t off = (uintptr_t) virt - kmemSpace.startAddr;
    // Loop through every page and map and add it
    pfn_t curPfn = (pfn_t) phys / NEXKE_CPU_PAGESZ;
    for (int i = 0; i < numPages; ++i)
    {
        MmPage_t* page = MmFindPagePfn (curPfn);
        assert (page);
        MmAddPage (&kmemSpace.entryList->obj->pageList, page, off + i * (NEXKE_CPU_PAGESZ));
        MmMulMapPage (&kmemSpace, (uintptr_t) virt + (i * NEXKE_CPU_PAGESZ), page, perm);
    }
    // Get address right
    virt += (uintptr_t) phys % NEXKE_CPU_PAGESZ;
    return virt;
}

// Unmaps MMIO / FW memory
void MmFreeKvMmio (void* virt)
{
    MmFreeKvRegion ((void*) CpuPageAlignDown (virt));
}

// Kernel backend functions
bool KvmInitObj (MmObject_t* obj)
{
    obj->pageable = false;
    return true;
}

bool KvmDestroyObj (MmObject_t* obj)
{
    return true;
}

bool KvmPageIn (MmObject_t* obj, uintptr_t offset)
{
    return true;
}

bool KvmPageOut (MmObject_t* obj, uintptr_t offset)
{
    return false;
}

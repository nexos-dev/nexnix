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

// Kernel free arena
typedef struct _kvarena
{
    MmKvPage_t* freeList;    // Free list of pages
    uintptr_t resvdPlace;    // Next place for a page structure

    size_t resvdSpace;    // Amount of resverved space left
    size_t numResvd;      // Number of pages reserved for free list
    size_t numFree;       // Number of free pages
    size_t numPages;      // Number of pages in arena

    size_t needsMap;    // Wheter this arena is mapped up front or needs to haves its pages mapped
    uintptr_t place;    // Next free place
    struct _kvarena* next;    // Next arena
} MmKvArena_t;

// Arena
static MmKvArena_t* mmArenas = NULL;

static MmSpace_t kmemSpace = {0};

// Boot pool globals
static uintptr_t bootPoolBase = 0;
static uintptr_t bootPoolMark = 0;
static uintptr_t bootPoolEnd = 0;
static size_t bootPoolSz = NEXBOOT_MEMPOOL_SZ;
static bool mmInit = false;    // Wheter normal MM is up yet

// Allocates a page from inside an arena
static MmKvPage_t* mmKvAllocInArena (MmKvArena_t* arena)
{
    // Check free list
    MmKvPage_t* page = arena->freeList;
    if (!page)
    {
        // Ensure we didn't overflow in amount of page structures
        assert (arena->resvdSpace >= sizeof (MmKvPage_t));
        // Else, we need to placement alloc
        // First allocate a page structure
        page = (MmKvPage_t*) arena->resvdPlace;
        arena->resvdPlace += sizeof (MmKvPage_t);
        arena->resvdSpace -= sizeof (MmKvPage_t);
        // Initialize page
        page->next = NULL;
        page->vaddr = arena->place;
        arena->place += NEXKE_CPU_PAGESZ;
    }
    else
        arena->freeList = page->next;
    // Map this page
    if (arena->needsMap)
    {
        // Allocate a physical page
        MmPage_t* physPage = MmAllocPage();
        if (!physPage)
            return NULL;
        // Add to object
        MmAddPage (&kmemSpace.entryList->obj->pageList,
                   physPage,
                   page->vaddr - kmemSpace.startAddr);
        // Map into virtual address space
        MmMulMapPage (&kmemSpace, page->vaddr, physPage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    }
    return page;
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
    // Figure out how many pages we need to reserve for free list
    size_t numPages = bootPoolSz / NEXKE_CPU_PAGESZ;
    size_t resvdSpace = CpuPageAlignUp ((numPages * sizeof (MmKvPage_t)) + sizeof (MmKvArena_t));
    // Allocate it
    bootPoolMark += resvdSpace;
    MmKvArena_t* arena = (MmKvArena_t*) bootPoolBase;
    // Prepare arena
    arena->freeList = NULL;
    arena->next = NULL;
    arena->resvdSpace = resvdSpace;
    arena->numFree = (bootPoolSz - resvdSpace) / NEXKE_CPU_PAGESZ;
    arena->numPages = arena->numFree;
    arena->numResvd = resvdSpace / NEXKE_CPU_PAGESZ;
    arena->place = bootPoolBase + resvdSpace;
    arena->needsMap = false;
    // Set next reserved spot
    arena->resvdPlace = (uintptr_t) bootPoolBase + sizeof (MmKvArena_t);
    mmArenas = arena;
}

// Second phase KVM init
void MmInitKvm2()
{
    // Setup kernel MM space
    kmemSpace.startAddr = NEXKE_KERNEL_ADDR_START;
    kmemSpace.endAddr = NEXKE_KERNEL_ADDR_END;
}

// Allocates a memory page for kernel
MmKvPage_t* MmAllocKvPage()
{
    // Find an arena with free memory
    MmKvArena_t* curArena = mmArenas;
    while (curArena)
    {
        if (curArena->numFree)
            return mmKvAllocInArena (curArena);    // Allocate page
        curArena = curArena->next;
    }
    return NULL;    // Out of memory
}

// Frees a memory page for kernel
void MmFreeKvPage (MmKvPage_t* page)
{
    // Figure out arena
    MmKvArena_t* arena = NULL;
    if (page->vaddr >= NEXKE_KERNEL_ADDR_START)
        arena = mmArenas->next;    // Arena is the general one
    else
        arena = mmArenas;    // Arena is boot pool
    assert (arena);
    page->next = arena->freeList;
    arena->freeList = page;
    arena->numFree++;
    if (arena->needsMap)
    {
        // Unmap page from arena
        MmPageList_t* pgList = &kmemSpace.entryList->obj->pageList;
        MmPage_t* physPage = MmLookupPage (pgList, page->vaddr - kmemSpace.startAddr);
        if (!physPage)
            NkPanic ("MmFreeKvPage: attempted to free unallocated page");
        MmRemovePage (pgList, physPage);
        // Free the page
        MmMulUnmapPage (&kmemSpace, page->vaddr);
        MmDeRefPage (physPage);
    }
}

// Returns kernel address space
MmSpace_t* MmGetKernelSpace()
{
    return &kmemSpace;
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

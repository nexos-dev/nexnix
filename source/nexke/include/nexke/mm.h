/*
    mm.h - contains MM subsystem headers
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

#ifndef _MM_H
#define _MM_H

#include <nexke/cpu.h>
#include <nexke/nexke.h>

// Bootstraps slab allocator
void MmSlabBootstrap();

// Initializes general purpose memory allocator
void MmMallocInit();

// Returns cache of given pointer
SlabCache_t* MmGetCacheFromPtr (void* ptr);

// Initialize page layer
void MmInitPage();

typedef paddr_t pfn_t;

// Page zone data structure
typedef struct _zone
{
    pfn_t pfn;                 // Base frame of zone
    int zoneIdx;               // Zone index
    size_t numPages;           // Number of pages in zone
    int freeCount;             // Number of free pages
    int flags;                 // Flags specifying type of memory in this zone
    struct _page* pfnMap;      // Table of PFNs in this zone
    struct _page* freeList;    // Free list of pages in this zone
} MmZone_t;

// Zone flags
#define MM_ZONE_KERNEL      (1 << 0)
#define MM_ZONE_MMIO        (1 << 1)
#define MM_ZONE_RESVD       (1 << 2)
#define MM_ZONE_RECLAIM     (1 << 3)
#define MM_ZONE_ALLOCATABLE (1 << 4)
#define MM_ZONE_NO_GENERIC  (1 << 5)    // Generic memory allocations are not allowed

// Page data structure
typedef struct _page
{
    pfn_t pfn;             // PFN of this page
    MmZone_t* zone;        // Zone this page resides in
    int refCount;          // Number of MmObject_t's using this page
    int state;             // State this page is in
    size_t offset;         // Offset in object. Used for page lookup
    struct _page* next;    // Links to track this page on a list of free / resident pages
    struct _page* prev;
} MmPage_t;

#define MM_PAGE_STATE_FREE      1
#define MM_PAGE_STATE_IN_OBJECT 2
#define MM_PAGE_STATE_UNUSABLE  3

#define MM_MAX_BUCKETS 512

// Page hash list type
typedef struct _pageHash
{
    MmPage_t* hashList[MM_MAX_BUCKETS];    // Hash list
} MmPageList_t;

// Page interface

// Allocates an MmPage, with specified characteristics
// Returns NULL if physical memory is exhausted
MmPage_t* MmAllocPage();

// Finds page at specified PFN, and removes it from free list
// If PFN is non-existant, or if PFN is in reserved memory region, returns PFN
// in state STATE_UNUSABLE
// Technically the page is usable, but only in certain situations (e.g., MMIO)
MmPage_t* MmFindPagePfn (pfn_t pfn);

// Frees an MmPage
void MmFreePage (MmPage_t* page);

// Allocates a contigious range of PFNs with specified at limit, beneath specified base adress
// NOTE: use with care, this function is poorly efficient
// Normally you don't need contigious PFNs
// Returns array of PFNs allocated
MmPage_t* MmAllocPagesAt (size_t count, paddr_t maxAddr, paddr_t align);

// Frees pages allocated with AllocPageAt
void MmFreePages (MmPage_t* pages, size_t count);

// Allocates page of memory from boot pool
// Return NULL if pool is exhausted
void* MmBootPoolAlloc();

// Dumps out page debugging info
void MmDumpPageInfo();

// Memory object types

typedef struct _memobject
{
    size_t count;             // Count of pages in this object
    int type;                 // Memory type represented by this object
    MmPageList_t pageList;    // List of pages allocated to this object
} MmObject_t;

// Page list management interface

// Adds a page to a page hash list
void MmAddPage (MmPageList_t* list, MmPage_t* page, size_t off);

// Looks up page in page list, returning NULL if none is found
MmPage_t* MmLookupPage (MmPageList_t* list, size_t off);

// Removes a page from the specified hash list
void MmRemovePage (MmPageList_t* list, MmPage_t* page);

// MUL basic interfaces

// MUL page flags
#define MUL_PAGE_R  (1 << 0)
#define MUL_PAGE_RW (1 << 1)
#define MUL_PAGE_KE (1 << 2)
#define MUL_PAGE_X  (1 << 3)
#define MUL_PAGE_CD (1 << 4)
#define MUL_PAGE_WT (1 << 5)

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags);

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt);

#endif

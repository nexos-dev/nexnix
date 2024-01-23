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
    pfn_t pfn;                   // Base frame of zone
    size_t numPages;             // Number of pages in zone
    int freeCount;               // Number of free pages
    int flags;                   // Flags specifying type of memory in this zone
    struct _page* pageTable;     // Table of PFNs in this zone
    struct _page* freeList;      // Free list of pages in this zone
    struct _page* zeroedList;    // List of zeroed pages in this zone
} MmZone_t;

// Zone flags
#define MM_ZONE_KERNEL      (1 << 0)
#define MM_ZONE_MMIO        (1 << 1)
#define MM_ZONE_RESVD       (1 << 2)
#define MM_ZONE_RECLAIMABLE (1 << 3)
#define MM_ZONE_NOZERO      (1 << 4)

// Page data structure
typedef struct _page
{
    pfn_t pfn;             // PFN of this page
    MmZone_t* zone;        // Zone this page resides in
    int refCount;          // Number of MmObject_t's using this page
    int state;             // State this page is in
    struct _page* next;    // Links to track this page on a list of free / used pages
    struct _page* prev;
} MmPage_t;

#define MM_PAGE_STATE_FREE     1
#define MM_PAGE_STATE_ZEROED   2
#define MM_PAGE_STATE_IN_CORE  3
#define MM_PAGE_STATE_UNUSABLE 4

// Page interface

// Allocates an MmPage, with specified characteristics
MmPage_t* MmAllocPage (int flags);

#define MM_PAGE_KERNEL (1 << 0)

// Finds page at specified PFN, and removes it from free list
// If PFN is non-existant, returns NULL; if PFN is in reserved memory region
// returns a MmPage_t with state MM_PAGE_STATE_UNUSABLE
// Technically the page is usable, but only in certain situations (e.g., MMIO)
MmPage_t* MmFindPagePfn (pfn_t pfn);

// Frees an MmPage
void MmFreePage (MmPage_t* page);

// Allocates page of memory from boot pool
// Return NULL if pool is exhausted
void* MmBootPoolAlloc();

// Memory object types

typedef struct _memobject
{
    uintptr_t vaddr;       // Virtual address obejct resides in
    size_t count;          // Count of pages in this object
    int type;              // Memory type represented by this object
    MmPage_t* pageList;    // List of pages allocated to this object
} MmObject_t;

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

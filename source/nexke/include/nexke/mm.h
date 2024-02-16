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

// Initializes boot pool
void MmInitKvm1();

// Bootstraps slab allocator
void MmSlabBootstrap();

// Initializes general purpose memory allocator
void MmMallocInit();

// Returns cache of given pointer
SlabCache_t* MmGetCacheFromPtr (void* ptr);

// Initialize page layer
void MmInitPage();

// Kernel memory management

// Kernel free virtual page structure
typedef struct _kvpage
{
    uintptr_t vaddr;    // Virtual address of page
    struct _kvpage* next;
} MmKvPage_t;

// Allocates a memory page for kernel
MmKvPage_t* MmAllocKvPage();

// Frees a memory page for kernel
void MmFreeKvPage (MmKvPage_t* page);

// Page management

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

#define MM_MAX_BUCKETS 256

// Page hash list type
typedef struct _pageHash
{
    MmPage_t* hashList[MM_MAX_BUCKETS];    // Hash list
    size_t maxBucket;                      // Highest used bucket
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

// References a page
void MmRefPage (MmPage_t* page);

// Dereferences a page
void MmDeRefPage (MmPage_t* page);

// Dereferences a page

// Allocates a contigious range of PFNs with specified at limit, beneath specified base adress
// NOTE: use with care, this function is poorly efficient
// Normally you don't need contigious PFNs
// Returns array of PFNs allocated
MmPage_t* MmAllocPagesAt (size_t count, paddr_t maxAddr, paddr_t align);

// Frees pages allocated with AllocPageAt
void MmFreePages (MmPage_t* pages, size_t count);

// Page list management interface

// Adds a page to a page hash list
void MmAddPage (MmPageList_t* list, MmPage_t* page, size_t off);

// Looks up page in page list, returning NULL if none is found
MmPage_t* MmLookupPage (MmPageList_t* list, size_t off);

// Removes a page from the specified hash list
void MmRemovePage (MmPageList_t* list, MmPage_t* page);

// Misc. page functions

// Dumps out page debugging info
void MmDumpPageInfo();

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

// MUL address space
typedef struct _mulspace* MmMulSpace_t;

// Maps page into address space
void MmMulMapPage (MmMulSpace_t* space, uintptr_t virt, MmPage_t* page, int perm);

// Unmaps page out of address space
void MmMulUnmapPage (MmMulSpace_t* space, uintptr_t virt);

// Gets mapping for specified virtual address
MmPage_t* MmMulGetMapping (MmMulSpace_t* space, uintptr_t virt);

// Creates an MUL address space
MmMulSpace_t* MmMulCreateSpace();

// Destroys an MUL address space
void MmMulDestroySpace (MmMulSpace_t* space);

// Memory object types

typedef struct _memobject
{
    size_t count;             // Count of pages in this object
    size_t resident;          // Number of pages resident in object
    int refCount;             // Reference count on object
    int backend;              // Memory backend type represented by this object
    int perm;                 // Memory permissions specified in MUL flags
    int inheritFlags;         // How this page is inherited by child processes
    bool pageable;            // Is object pageable
    MmPageList_t pageList;    // List of pages allocated to this object
    void** backendTab;        // Table of backend functions
    void* backendData;        // Data used by backend in this object
} MmObject_t;

// Memory object backends
#define MM_BACKEND_ANON   0
#define MM_BACKEND_KERNEL 1
#define MM_BACKEND_MAX    2

// Backend functions
#define MM_BACKEND_PAGEIN      0
#define MM_BACKEND_PAGEOUT     1
#define MM_BACKEND_INIT_OBJ    2
#define MM_BACKEND_DESTROY_OBJ 3

typedef bool (*MmPageIn) (MmObject_t*, uintptr_t);
typedef bool (*MmPageOut) (MmObject_t*, uintptr_t);
typedef bool (*MmBackendInit) (MmObject_t*);
typedef bool (*MmBackendDestroy) (MmObject_t*);

// Functions to call backend
#define MmBackendPageIn(object, offset) \
    (((MmPageIn) (object)->backendTab[MM_BACKEND_PAGEIN]) ((object), (offset)))
#define MmBackendPageOut(object, offset) \
    (((MmPageOut) (object)->backendTab[MM_BACKEND_PAGEOUT]) ((object), (offset)))
#define MmBackendInit(object) \
    (((MmBackendInit) (object)->backendTab[MM_BACKEND_INIT_OBJ]) ((object)))
#define MmBackendDestroy(object) \
    (((MmBackendDestroy) (object)->backendTab[MM_BACKEND_DESTROY_OBJ]) ((object)))

// Memory object functions

// Creates a new memory object
MmObject_t* MmCreateObject (size_t pages, int backend, int perm);

// References a memory object
void MmRefObject (MmObject_t* object);

// References a memory object
void MmDeRefObject (MmObject_t* object);

// Applies new permissions to object
void MmProtectObject (MmObject_t* object, int newPerm);

// Initializes object system
void MmInitObject();

// Address space types
typedef struct _mementry
{
    uintptr_t vaddr;           // Virtual address
    size_t count;              // Number of pages in entry
    MmObject_t* obj;           // Object represented by address space entry
    struct _mementry* next;    // Next entry
    struct _mementry* prev;    // Last entry
} MmSpaceEntry_t;

typedef struct _memspace
{
    uintptr_t startAddr;          // Address the address space starts at
    uintptr_t endAddr;            // End address
    MmSpaceEntry_t* entryList;    // List of address space entries
    MmMulSpace_t* mulSpace;       // MUL address space
} MmSpace_t;

// Creates a new empty address space
MmSpace_t* MmCreateSpace();

// Destroys an address space
void MmDestroySpace();

// Allocates an address space entry
MmSpaceEntry_t* MmAllocSpace (MmSpace_t* as, MmObject_t* obj, uintptr_t hintAddr, size_t numPages);

// Frees an address space entry
void MmFreeSpace (MmSpace_t* as, MmSpaceEntry_t* entry);

// Finds address space entry for given address
MmSpaceEntry_t* MmFindSpaceEntry (MmSpace_t* as, uintptr_t addr);

// Dumps address space
void MmDumpSpace (MmSpace_t* as);

// Initializes boot pool
void MmInitKvm1();

#endif

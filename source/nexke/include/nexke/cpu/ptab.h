/*
    ptab.h - contains page table management stuff
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

#ifndef _PTAB_H
#define _PTAB_H

#include <nexke/cpu.h>
#include <nexke/types.h>
#include <stdbool.h>

// Page table cache entry
typedef struct _ptcache
{
    uintptr_t addr;           // Address of this entry
    paddr_t ptab;             // Physical address of page table being mapped
    pte_t* pte;               // PTE we should use to map this to a physical address
    int level;                // Level of this cache entry
    bool inUse;               // If this entry is in use
    struct _ptcache* next;    // Next entry in list
    struct _ptcache* prev;
} MmPtCacheEnt_t;

#define MM_PTAB_MAX_LEVEL 8

#define MM_PTAB_UNCACHED 0

typedef struct _mmspace
{
    paddr_t base;                                     // Physical base of top level table
    MmPtCacheEnt_t* ptFreeList;                       // List of free PT cache entries
    MmPtCacheEnt_t* ptLists[MM_PTAB_MAX_LEVEL];       // List of in use entries for each level
    MmPtCacheEnt_t* ptListsEnd[MM_PTAB_MAX_LEVEL];    // List tails for each levels
    bool tlbUpdatePending;                            // Is a TLB update pending?
                              // Used to lazily update the TLB on CPUs where that is slow
    int freeCount;              // Free number of cache entries
    MmPageList_t tablePages;    // Page table pages
#ifdef NEXNIX_ARCH_I386
    int keVersion;    // Kernel page table version
#endif
} MmMulSpace_t;

// Initializes page table manager
void MmPtabInit (int numLevels);

// Walks to a page table entry and maps specfied value into it
void MmPtabWalkAndMap (MmSpace_t* space, paddr_t as, uintptr_t vaddr, pte_t pteVal);

// Walks to a page table entry and unmaps it
void MmPtabWalkAndUnmap (MmSpace_t* space, paddr_t as, uintptr_t vaddr);

// Walks to a page table entry and returns it
pte_t MmPtabGetPte (MmSpace_t* space, paddr_t as, uintptr_t vaddr);

// Walks to a page table entry and changes its protection
void MmPtabWalkAndChange (MmSpace_t* space, paddr_t as, uintptr_t vaddr, pte_t perm);

// Initializes PT cache in specified space
void MmPtabInitCache (MmSpace_t* space);

// Grabs cache entry for table
MmPtCacheEnt_t* MmPtabGetCache (paddr_t ptab, int level);

// Returns cache entry
void MmPtabReturnCache (MmPtCacheEnt_t* cacheEnt);

// Frees cache entry to free list
void MmPtabFreeToCache (MmPtCacheEnt_t* cacheEnt);

// Returns entry and gets new entry
MmPtCacheEnt_t* MmPtabSwapCache (paddr_t ptab, MmPtCacheEnt_t* cacheEnt, int level);

// Flushes a single TLB entry
void MmMulFlush (uintptr_t vaddr);

#endif

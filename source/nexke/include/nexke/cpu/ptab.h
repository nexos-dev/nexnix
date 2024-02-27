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
#include <stdbool.h>

// Page table cache entry
typedef struct _ptcache
{
    uintptr_t addr;           // Address of this entry
    paddr_t ptab;             // Physical address of page table being mapped
    pte_t* pte;               // PTE we should use to map this to a physical address
    bool highPrio;            // If this entry is high priority
    bool inUse;               // If this entry is in use
    struct _ptcache* next;    // Next entry in list
    struct _ptcache* prev;
} MmPtCacheEnt_t;

typedef struct _page MmPage_t;
typedef struct _memspace MmSpace_t;

typedef struct _mmspace
{
    paddr_t base;                     // Physical base of top level table
    MmPtCacheEnt_t* ptFreeList;       // List of free PT cache entries
    MmPtCacheEnt_t* ptUsedList;       // List of used PT cache entries
    MmPtCacheEnt_t* ptUsedListEnd;    // List of used PT cache entries tail
    int lowPrioCount;                 // Number of low priority PT cache entries available
    bool tlbUpdatePending;            // Is a TLB update pending?
                                      // Used to lazily update the TLB on CPUs where that is slow
} MmMulSpace_t;

// Initializes page table manager
void MmPtabInit (int numLevels);

// Walks to a page table entry and maps specfied value into it
void MmPtabWalkAndMap (MmSpace_t* space, paddr_t asPhys, uintptr_t vaddr, pte_t pteVal);

// Walks to a page table entry and unmaps it
void MmPtabWalkAndUnmap (MmSpace_t* space, paddr_t asPhys, uintptr_t vaddr);

// Walks to a page table entry and returns it
pte_t MmPtabGetPte (MmSpace_t* space, paddr_t asPhys, uintptr_t vaddr);

// Initializes PT cache in specified space
void MmPtabInitCache (MmSpace_t* space);

// Grabs cache entry for table
MmPtCacheEnt_t* MmPtabGetCache (MmSpace_t* space, paddr_t ptab, bool highPrio);

// Returns cache entry
void MmPtabReturnCache (MmSpace_t* space, MmPtCacheEnt_t* cacheEnt);

// Returns entry and gets new entry
MmPtCacheEnt_t* MmPtabSwapCache (MmSpace_t* space,
                                 paddr_t ptab,
                                 MmPtCacheEnt_t* cacheEnt,
                                 bool highPrio);

// Flushes a single TLB entry
void MmMulFlush (uintptr_t vaddr);

#endif

/*
    ptab.c - contains architecture independent page table manager
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
#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Number of levels of paging structures
static int mmNumLevels = 0;

// Tunables
#define MM_PTAB_MINFREE    2
#define MM_PTAB_FREETARGET 8

// Initializes page table manager
void MmPtabInit (int numLevels)
{
    mmNumLevels = numLevels;
}

// Walks to a page table entry and maps specfied value into it
void MmPtabWalkAndMap (MmSpace_t* space, paddr_t as, uintptr_t vaddr, pte_t pteVal)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, as, MUL_IDX_PRIO (mmNumLevels));
    for (int i = mmNumLevels; i > 1; --i)
    {
        pte_t* curSt = (pte_t*) cacheEnt->addr;
        pte_t* ent = &curSt[MUL_IDX_LEVEL (vaddr, i)];
        if (*ent)
        {
            // Verify validity of this table for mapping this page
            // Failure results in panic
            MmMulVerify (*ent, pteVal);
            // Grab cache entry
            cacheEnt = MmPtabSwapCache (space, PT_GETFRAME (*ent), cacheEnt, MUL_IDX_PRIO (i - 1));
        }
        else
        {
            // Allocate new table
            // This calls architecture specific code
            paddr_t newTab = MmMulAllocTable (space, vaddr, curSt, ent);
            cacheEnt = MmPtabSwapCache (space, newTab, cacheEnt, MUL_IDX_PRIO (i - 1));
        }
    }
    // Map last entry
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t* lastEnt = &lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    // Map it
    *lastEnt = pteVal;
    // Return cache entry
    MmPtabReturnCache (space, cacheEnt);
}

// Walks to a pte and returns a cache entry
static MmPtCacheEnt_t* mmPtabWalk (MmSpace_t* space, paddr_t as, uintptr_t vaddr)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, as, MUL_IDX_PRIO (mmNumLevels));
    for (int i = mmNumLevels; i > 1; --i)
    {
        pte_t* curSt = (pte_t*) cacheEnt->addr;
        pte_t* ent = &curSt[MUL_IDX_LEVEL (vaddr, i)];
        if (*ent)
        {
            // Grab cache entry
            cacheEnt = MmPtabSwapCache (space, PT_GETFRAME (*ent), cacheEnt, MUL_IDX_PRIO (i - 1));
        }
        else
        {
            // Table doesn't exist, panic
            NkPanic ("nexke: error: attempting to unmap invalid mapping");
        }
    }
    return cacheEnt;
}

// Walks to a page table entry and unmaps it
void MmPtabWalkAndUnmap (MmSpace_t* space, paddr_t as, uintptr_t vaddr)
{
    // Grab mapping
    MmPtCacheEnt_t* cacheEnt = mmPtabWalk (space, as, vaddr);
    // Map last entry
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t* lastEnt = &lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    // Unmap it
    *lastEnt = 0;
    // Return cache entry
    MmPtabReturnCache (space, cacheEnt);
}

// Walks to a page table entry and changes its protection
void MmPtabWalkAndChange (MmSpace_t* space, paddr_t as, uintptr_t vaddr, pte_t perm)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = mmPtabWalk (space, as, vaddr);
    // Map last entry
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t* lastEnt = &lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    // Change value
    MmMulChangePte (lastEnt, perm);
    MmPtabReturnCache (space, cacheEnt);
}

// Walks to a page table entry and returns it's mapping
pte_t MmPtabGetPte (MmSpace_t* space, paddr_t as, uintptr_t vaddr)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = mmPtabWalk (space, as, vaddr);
    // Get mapping
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t pte = lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    MmPtabReturnCache (space, cacheEnt);
    return pte;
}

// Zeroes a page with the MUL
// This function is the same across MULs so it is implemented in the architecture independent module
void MmMulZeroPage (MmSpace_t* space, MmPage_t* page)
{
    // Get physical address
    paddr_t addr = page->pfn * NEXKE_CPU_PAGESZ;
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, addr, false);
    memset ((void*) cacheEnt->addr, 0, NEXKE_CPU_PAGESZ);
    // Free the cache entry
    MmPtabFreeToCache (space, cacheEnt);
}

// Initializes PT cache in specified space
void MmPtabInitCache (MmSpace_t* space)
{
    // Initialize the cache entries
    MmPtCacheEnt_t* entries = (MmPtCacheEnt_t*) MUL_PTCACHE_ENTRY_BASE;
    for (int i = 0; i < MUL_MAX_PTCACHE; ++i)
    {
        entries[i].addr = MUL_PTCACHE_BASE + (i * NEXKE_CPU_PAGESZ);
        // Grab address of PTE for this address
        entries[i].pte = MmMulGetCacheAddr (entries[i].addr);
        // Check if this is the end
        if ((i + 1) == MUL_MAX_PTCACHE)
            entries[i].next = NULL;
        else
            entries[i].next = &entries[i + 1];
        if (i == 0)
            entries[i].prev = NULL;
        else
            entries[i].prev = &entries[i - 1];
    }
    // Set pointer head
    space->mulSpace.ptFreeList = (MmPtCacheEnt_t*) MUL_PTCACHE_ENTRY_BASE;
}

// Returns entry and gets new entry
MmPtCacheEnt_t* MmPtabSwapCache (MmSpace_t* space,
                                 paddr_t ptab,
                                 MmPtCacheEnt_t* cacheEnt,
                                 bool highPrio)
{
    cacheEnt->inUse = false;
    return MmPtabGetCache (space, ptab, highPrio);
}

// Grabs cache entry for table
MmPtCacheEnt_t* MmPtabGetCache (MmSpace_t* space, paddr_t ptab, bool highPrio)
{
    // Find entry on cache
    MmPtCacheEnt_t* ent = space->mulSpace.ptUsedList;
    while (ent)
    {
        if (ent->ptab == ptab)
        {
            // We found it
            ent->inUse = true;
            return ent;
        }
        ent = ent->next;
    }
    // Attempt to find completly free entry
    ent = space->mulSpace.ptFreeList;
    if (ent)
    {
        // Remove from list
        space->mulSpace.ptFreeList = ent->next;
        space->mulSpace.ptFreeList->prev = NULL;
        // Set it up
        ent->highPrio = highPrio;
        if (!highPrio)
            ++space->mulSpace.lowPrioCount;    // Increase counter
        // Add to used list
        if (space->mulSpace.ptUsedList)
            space->mulSpace.ptUsedList->prev = ent;    // Set head back link
        else
            space->mulSpace.ptUsedListEnd = ent;    // Set tail
        ent->next = space->mulSpace.ptUsedList;
        ent->prev = NULL;
        space->mulSpace.ptUsedList = ent;
        --space->mulSpace.freeCount;
        goto setup;
    }
    // No available cache entry, we need to evict one
    // Determine if we can evict high priority entries
    bool evictHighPrio = false;
    if (highPrio || space->mulSpace.lowPrioCount == 0)
        evictHighPrio = true;
    ent = space->mulSpace.ptUsedListEnd;
    MmPtCacheEnt_t* foundEnt = NULL;
    while (ent)
    {
        // Make sure entry is not in use
        if (!ent->inUse)
        {
            // Check priority now
            if (!ent->highPrio || evictHighPrio || highPrio)
            {
                foundEnt = ent;
                break;
            }
        }
        ent = ent->prev;
    }
    if (!foundEnt)
    {
        assert (false);    // TODO: block for entry to be released
    }
    // Set it up
    // If we are going from high to low priority, or low to high priority, note that
    if (ent->highPrio && !highPrio)
    {
        // Increase total low priority entries
        ++space->mulSpace.lowPrioCount;
    }
    else if (!ent->highPrio && highPrio)
    {
        // Decrease
        --space->mulSpace.lowPrioCount;
    }
setup:
    ent->highPrio = highPrio;
    ent->inUse = true;
    ent->ptab = ptab;
    MmMulMapCacheEntry (ent->pte, ent->ptab);
    MmMulFlushCacheEntry (ent->addr);
    return ent;
}

// Returns cache entry to cache
void MmPtabReturnCache (MmSpace_t* space, MmPtCacheEnt_t* cacheEnt)
{
    cacheEnt->inUse = false;
    // Check if we need to free any entries
    if (space->freeCount < MM_PTAB_MINFREE)
    {
        MmMulSpace_t* mulSpace = &space->mulSpace;
        // Get to free target. To move entries from used to free, we go from the tail
        MmPtCacheEnt_t* curEnt = mulSpace->ptUsedListEnd;
        while (curEnt)
        {
            // Evict if not in use and not high priority
            if (!curEnt->highPrio && !curEnt->inUse)
            {
                // Free it
                MmPtabFreeToCache (space, curEnt);
                // Check if we hit target
                if (mulSpace->freeCount >= MM_PTAB_FREETARGET)
                    break;
            }
        }
    }
}

// Frees cache entry to free list
void MmPtabFreeToCache (MmSpace_t* space, MmPtCacheEnt_t* cacheEnt)
{
    cacheEnt->inUse = false;
    MmMulSpace_t* mulSpace = &space->mulSpace;
    // Remove from used list
    if (cacheEnt == mulSpace->ptUsedListEnd)
        mulSpace->ptUsedListEnd = cacheEnt->prev;
    if (cacheEnt == mulSpace->ptUsedList)
        mulSpace->ptUsedList = cacheEnt->next;
    if (cacheEnt->prev)
        cacheEnt->prev->next = cacheEnt->next;
    if (cacheEnt->next)
        cacheEnt->next->prev = cacheEnt->prev;
    // Add to front of free list
    cacheEnt->prev = NULL;
    cacheEnt->next = mulSpace->ptFreeList;
    if (mulSpace->ptFreeList)
        mulSpace->ptFreeList->prev = cacheEnt;
    mulSpace->ptFreeList = cacheEnt;
    // Update counter
    ++mulSpace->freeCount;
    if (!cacheEnt->highPrio)
        --mulSpace->lowPrioCount;
}

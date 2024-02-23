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
#include <nexke/cpu/ptab.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Number of levels of paging structures
static int mmNumLevels = 0;

// Initializes page table manager
void MmPtabInit (int numLevels)
{
    mmNumLevels = numLevels;
}

// Walks to a page table entry and maps specfied value into it
void MmPtabWalkAndMap (MmSpace_t* space, uintptr_t vaddr, bool isKernel, pte_t pteVal)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base);
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
            cacheEnt = MmPtabSwapCache (space, PT_GETFRAME (*ent), cacheEnt);
        }
        else
        {
            // Allocate new table
            // This calls architecture specific code
            paddr_t newTab = MmMulAllocTable (space, curSt, ent, isKernel);
            cacheEnt = MmPtabSwapCache (space, newTab, cacheEnt);
        }
    }
    // Map last entry
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t* lastEnt = &lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    // Map it
    *lastEnt = pteVal;
    // Return cache entry
    MmPtabReturnCache (space, cacheEnt);
    // Flush TLB
    MmMulFlush (vaddr);
}

// Walks to a page table entry and unmaps it
void MmPtabWalkAndUnmap (MmSpace_t* space, uintptr_t vaddr)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base);
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
            cacheEnt = MmPtabSwapCache (space, PT_GETFRAME (*ent), cacheEnt);
        }
        else
        {
            // Table doesn't exist, panic
            NkPanic ("nexke: error: attempting to unmap invalid mapping");
        }
    }
    // Map last entry
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t* lastEnt = &lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    // Unmap it
    *lastEnt = 0;
    // Return cache entry
    MmPtabReturnCache (space, cacheEnt);
    // Flush TLB
    MmMulFlush (vaddr);
}

// Walks to a page table entry and returns it
pte_t MmPtabGetPte (MmSpace_t* space, uintptr_t vaddr)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base);
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
            cacheEnt = MmPtabSwapCache (space, PT_GETFRAME (*ent), cacheEnt);
        }
        else
        {
            // Allocate new table
            // This calls architecture specific code
            paddr_t newTab = MmMulAllocTable (space, curSt, ent, isKernel);
            cacheEnt = MmPtabSwapCache (space, newTab, cacheEnt);
        }
    }
    // Map last entry
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    pte_t* lastEnt = &lastSt[MUL_IDX_LEVEL (vaddr, 1)];
    // Map it
    *lastEnt = pteVal;
    // Return cache entry
    MmPtabReturnCache (space, cacheEnt);
    // Flush TLB
    MmMulFlush (vaddr);
}

// Walks to a page table entry and unmaps it
void MmPtabWalkAndUnmap (MmSpace_t* space, uintptr_t vaddr)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base);
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
            cacheEnt = MmPtabSwapCache (space, PT_GETFRAME (*ent), cacheEnt);
        }
        else
        {
            // Table doesn't exist, panic
            NkPanic ("nexke: error: attempting to unmap invalid mapping");
        }
    }
    // Get mapping
    pte_t* lastSt = (pte_t*) cacheEnt->addr;
    return lastSt[MUL_IDX_LEVEL (vaddr, 1)];
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
    }
    // Set pointer head
    space->mulSpace.ptCache = (MmPtCacheEnt_t*) MUL_PTCACHE_ENTRY_BASE;
}

// Returns entry and gets new entry
MmPtCacheEnt_t* MmPtabSwapCache (MmSpace_t* space, paddr_t ptab, MmPtCacheEnt_t* cacheEnt)
{
    MmPtabReturnCache (space, cacheEnt);
    return MmPtabGetCache (space, ptab);
}

// Grabs cache entry for table
MmPtCacheEnt_t* MmPtabGetCache (MmSpace_t* space, paddr_t ptab)
{
    // Grab entry from head
    MmPtCacheEnt_t* entry = space->mulSpace.ptCache;
    assert (entry);    // TODO: block here for entries to be returned
    // Advance list
    space->mulSpace.ptCache = entry->next;
    // Map it
    MmMulMapCacheEntry (entry->pte, ptab);
    // Flush TLB entry
    MmMulFlush (entry->addr);
    return entry;
}

// Returns cache entry to cache
void MmPtabReturnCache (MmSpace_t* space, MmPtCacheEnt_t* cacheEnt)
{
    // Add to head of list
    cacheEnt->next = space->mulSpace.ptCache;
    space->mulSpace.ptCache = cacheEnt;
}

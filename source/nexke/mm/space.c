/*
    space.c - contains address space management code
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
#include <nexke/nexke.h>

// Address spaces start at 64 KiB so that null pointer references
// always crash instead of causing corruption
#define MM_SPACE_USER_START 0x10000

// Slab caches
static SlabCache_t* mmSpaceCache = NULL;
static SlabCache_t* mmEntryCache = NULL;

// Creates a new empty address space
MmSpace_t* MmCreateSpace()
{
    MmSpace_t* newSpace = MmCacheAlloc (mmSpaceCache);
    if (!newSpace)
        NkPanic ("nexke: out of memory");
    newSpace->startAddr = MM_SPACE_USER_START;
    newSpace->endAddr = NEXKE_USER_ADDR_END;
    return newSpace;
}

// Destroys address space
void MmDestroySpace (MmSpace_t* space)
{
    // Free every allocated space entry
    MmSpaceEntry_t* curEntry = space->entryList;
    while (curEntry)
    {
        MmFreeSpace (space, curEntry);
        curEntry = curEntry->next;
    }
    MmCacheFree (mmEntryCache, space);
}

// Allocates an address space entry for object
MmSpaceEntry_t* MmAllocSpace (MmSpace_t* as, MmObject_t* obj, uintptr_t hintAddr, size_t numPages)
{
    // Ensure hintAddr is before address space end and after start
    if (hintAddr && (hintAddr >= as->endAddr || hintAddr < as->startAddr))
        return NULL;
    // Find a free hole, but first, advance to hintAddr
    MmSpaceEntry_t* nextEntry = as->entryList;
    MmSpaceEntry_t* lastEntry = nextEntry;
    uintptr_t curAddr = as->startAddr;
    while (nextEntry)
    {
        curAddr = nextEntry->vaddr + (nextEntry->count * NEXKE_CPU_PAGESZ);
        // Check if this entry exceeds the hint
        if ((nextEntry->vaddr + (nextEntry->count * NEXKE_CPU_PAGESZ)) >= hintAddr)
            break;
        lastEntry = nextEntry;
        nextEntry = nextEntry->next;
    }
    bool entryFound = false;
    // If loop to get us to hint reached end of list, then we don't need to find a free region
    if (nextEntry)
    {
        while ((nextEntry = nextEntry->next) != NULL)
        {
            // Check if end of last entry is less than start of this entry
            if ((nextEntry->vaddr - curAddr) >= (numPages * NEXKE_CPU_PAGESZ))
            {
                // This hole checks out
                entryFound = true;
                break;
            }
            lastEntry = nextEntry;
            curAddr = nextEntry->vaddr + (nextEntry->count * NEXKE_CPU_PAGESZ);
        }
    }
    // Figure out end of free region
    uintptr_t freeAreaEnd = 0;
    if (nextEntry)
        freeAreaEnd = nextEntry->vaddr;
    else
        freeAreaEnd = as->endAddr;
    // If we reached the end without finding a region, there may be space at end of address space
    if (!entryFound)
    {
        if ((as->endAddr - curAddr) >= (numPages * NEXKE_CPU_PAGESZ))
        {
            // We do have space at end
            entryFound = true;
        }
        else
        {
            // FIXME: if there was a hint address, we may end up ignoring potentially
            // free memory under the hint address. This should be fixed
            return NULL;
        }
    }
    assert (entryFound);
    // We found an address space entry, now we need to add it to the list
    MmSpaceEntry_t* newEntry = MmCacheAlloc (mmEntryCache);
    if (!newEntry)
        NkPanicOom();
    // Figure out where to put this entry. If we have a hint, we would like to keep it at the
    // hint if at all possible
    if (hintAddr && hintAddr >= curAddr && hintAddr < freeAreaEnd)
        newEntry->vaddr = hintAddr;
    else
        newEntry->vaddr = curAddr;
    newEntry->count = numPages;
    newEntry->obj = obj;
    // Add to list
    newEntry->next = nextEntry;
    newEntry->prev = lastEntry;
    if (newEntry->next)
        newEntry->next->prev = newEntry;
    if (newEntry->prev)
        newEntry->prev->next = newEntry;
    if (newEntry->prev == NULL)
        as->entryList = newEntry;
    return newEntry;
}

// Frees an address space entry
void MmFreeSpace (MmSpace_t* as, MmSpaceEntry_t* entry)
{
    // Remove from list of entries
    if (entry->next)
        entry->next->prev = entry->prev;
    if (entry->prev)
        entry->prev->next = entry->next;
    if (entry == as->entryList)
        as->entryList = entry->next;
    // Free entry
    MmCacheFree (mmEntryCache, entry);
}

// Finds address space entry for given address
MmSpaceEntry_t* MmFindSpaceEntry (MmSpace_t* as, uintptr_t addr)
{
    MmSpaceEntry_t* curEntry = as->entryList;
    while (curEntry)
    {
        uintptr_t entryEnd = curEntry->vaddr + (curEntry->count * NEXKE_CPU_PAGESZ);
        if (addr >= curEntry->vaddr && addr < entryEnd)
            return curEntry;    // Found it!
        curEntry = curEntry->next;
    }
    return NULL;
}

// Dumps address space
void MmDumpSpace (MmSpace_t* as)
{
    MmSpaceEntry_t* entry = as->entryList;
    while (entry)
    {
        NkLogDebug ("Found address space entry: base %#llX, page count %d\n",
                    (uint64_t) entry->vaddr,
                    entry->count);
        entry = entry->next;
    }
}

// Initialization routines
// These routines initialize the MM system

// Bootstraps memory manager
void MmInitPhase1()
{
    // Initialize KVM
    MmInitKvm1();
    // Bootstrap slab allocator
    MmSlabBootstrap();
    // Initialize malloc
    MmMallocInit();
}

// Initializes page frame manager, MUL, and kernel address space
void MmInitPhase2()
{
    // Initialize page frame manager
    MmInitPage();
    // Initialize object management
    MmInitObject();
    // Set up caches
    mmSpaceCache = MmCacheCreate (sizeof (MmSpace_t), NULL, NULL);
    mmEntryCache = MmCacheCreate (sizeof (MmSpaceEntry_t), NULL, NULL);
    if (!mmSpaceCache || !mmEntryCache)
        NkPanicOom();
}

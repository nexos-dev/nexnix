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
#include <string.h>

// Address spaces start at 64 KiB so that null pointer references
// always crash instead of causing corruption
#define MM_SPACE_USER_START 0x10000

// Slab caches
static SlabCache_t* mmSpaceCache = NULL;
static SlabCache_t* mmEntryCache = NULL;

// Currently active address space
static MmSpace_t* mmCurSpace = NULL;

// Gets end of entry
static inline uintptr_t mmEntryEnd (MmSpaceEntry_t* entry)
{
    return entry->vaddr + (entry->count * NEXKE_CPU_PAGESZ);
}

// Creates a new empty address space
MmSpace_t* MmCreateSpace()
{
    MmSpace_t* newSpace = MmCacheAlloc (mmSpaceCache);
    memset (newSpace, 0, sizeof (MmSpace_t));
    if (!newSpace)
        NkPanic ("nexke: out of memory");
    newSpace->startAddr = MM_SPACE_USER_START;
    newSpace->endAddr = NEXKE_USER_ADDR_END;
    // Create a fake entry
    MmSpaceEntry_t* fake = MmCacheAlloc (mmEntryCache);
    if (!fake)
        NkPanicOom();
    memset (fake, 0, sizeof (MmSpaceEntry_t));
    fake->vaddr = newSpace->startAddr;
    newSpace->entryList = fake;
    // Create fake end
    MmSpaceEntry_t* fakeEnd = MmCacheAlloc (mmEntryCache);
    if (!fakeEnd)
        NkPanicOom();
    memset (fakeEnd, 0, sizeof (MmSpaceEntry_t));
    fakeEnd->vaddr = newSpace->endAddr;
    fakeEnd->prev = fake;
    fake->next = fakeEnd;
    return newSpace;
}

// Destroys address space
void MmDestroySpace (MmSpace_t* space)
{
    assert (space != MmGetKernelSpace());    // Can't operate on kernel space
    // Free every allocated space entry
    MmSpaceEntry_t* curEntry = space->entryList;
    while (curEntry->vaddr != space->endAddr)
    {
        MmFreeSpace (space, curEntry);
        curEntry = curEntry->next;
    }
    // Free both fake entries
    MmCacheFree (mmEntryCache, space->entryList->next);
    MmCacheFree (mmEntryCache, space->entryList);
    MmCacheFree (mmSpaceCache, space);
}

// Adds entry to space
static void mmAddEntry (MmSpace_t* space, MmSpaceEntry_t* prec, MmSpaceEntry_t* new)
{
    new->next = prec->next;
    new->prev = prec;
    new->next->prev = new;
    new->prev->next = new;
    ++space->numEntries;
}

// Removes entry
static void mmRemoveEntry (MmSpace_t* space, MmSpaceEntry_t* entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    --space->numEntries;
}

static MmSpaceEntry_t* mmFindEntryUnlocked (MmSpace_t* space, uintptr_t addr)
{
    MmSpaceEntry_t* cur = space->entryList;
    while (cur->vaddr != space->endAddr)
    {
        // CHeck if we are inside this entry
        uintptr_t curEnd = mmEntryEnd (cur);
        if (cur->vaddr <= addr && curEnd >= addr)
            return cur;    // We have a match
        // Now check if this address is free and this entry precedes said free area
        uintptr_t upperBound = cur->next->vaddr;
        if (addr > curEnd && addr < upperBound)
            return cur;    // Found preceding entry
        // Check if we are done
        if (upperBound > addr)
            break;    // Nothing left to do
        cur = cur->next;
    }
    return NULL;
}

// Finds free address based on hint. Returns preceding entry
static MmSpaceEntry_t* mmFindFree (MmSpace_t* space, uintptr_t* addr, size_t numPages)
{
    uintptr_t hint = *addr;
    if (hint > space->endAddr && hint < space->startAddr)
        return NULL;    // Can't use out of bounds hint
    // Find entry closest to hint
    MmSpaceEntry_t* cur = NULL;
    if (hint == 0)
        cur = space->entryList;
    else
        cur = mmFindEntryUnlocked (space, hint);
    while (cur->vaddr != space->endAddr)
    {
        // See if there is enough free space after this entry
        MmSpaceEntry_t* next = cur->next;
        if (next->vaddr - mmEntryEnd (cur) >= (numPages * NEXKE_CPU_PAGESZ))
        {
            // If there was numPages between next entry start and current entry end and next entry
            // start, use this hole
            *addr = mmEntryEnd (cur);
            return cur;
        }
        cur = next;
    }
    return NULL;
}

// Allocates an address space entry for object
MmSpaceEntry_t* MmAllocSpace (MmSpace_t* space,
                              MmObject_t* obj,
                              uintptr_t hintAddr,
                              size_t numPages)
{
    assert (space != MmGetKernelSpace());    // Can't operate on kernel space
    // Get free space
    uintptr_t addr = hintAddr;
    NkSpinLock (&space->lock);
    MmSpaceEntry_t* prevEntry = mmFindFree (space, &addr, numPages);
    if (!prevEntry)
    {
        // Try again without the hint, as there may have been memory beneath the hint that we missed
        addr = 0;
        prevEntry = mmFindFree (space, &addr, numPages);
        if (!prevEntry)
        {
            NkSpinUnlock (&space->lock);
            return NULL;    // Not enough space
        }
    }
    // Create a new entry
    MmSpaceEntry_t* newEntry = MmCacheAlloc (mmEntryCache);
    if (!newEntry)
        NkPanicOom();
    newEntry->count = numPages;
    newEntry->vaddr = addr;
    newEntry->obj = obj;
    mmAddEntry (space, prevEntry, newEntry);
    NkSpinUnlock (&space->lock);
    return newEntry;
}

// Frees an address space entry
void MmFreeSpace (MmSpace_t* space, MmSpaceEntry_t* entry)
{
    NkSpinLock (&space->lock);
    assert (space != MmGetKernelSpace());    // Can't operate on kernel space
    mmRemoveEntry (space, entry);
    MmDeRefObject (entry->obj);
    MmCacheFree (mmEntryCache, entry);
    NkSpinUnlock (&space->lock);
}

// Finds address space entry for given address
MmSpaceEntry_t* MmFindSpaceEntry (MmSpace_t* space, uintptr_t addr)
{
    NkSpinLock (&space->lock);
    MmSpaceEntry_t* entry = mmFindEntryUnlocked (space, addr);
    NkSpinUnlock (&space->lock);
    return entry;
}

// Finds faulting entry
MmSpaceEntry_t* MmFindFaultEntry (MmSpace_t* space, uintptr_t addr)
{
    NkSpinLock (&space->lock);
    // Check hint
    if (space->faultHint)
    {
        if (space->faultHint->vaddr <= addr && mmEntryEnd (space->faultHint) >= addr)
        {
            NkSpinUnlock (&space->lock);
            return space->faultHint;
        }
    }
    // Find it
    MmSpaceEntry_t* cur = space->entryList;
    while (cur && cur->vaddr != space->endAddr)
    {
        // CHeck if we are inside this entry
        if (cur->vaddr <= addr && mmEntryEnd (cur) >= addr)
        {
            space->faultHint = cur;
            NkSpinUnlock (&space->lock);
            return cur;    // We have a match
        }
        cur = cur->next;
    }
    NkSpinUnlock (&space->lock);
    return NULL;
}

// Creates kernel space
void MmCreateKernelSpace (MmObject_t* kernelObj)
{
    NkLogDebug ("nexke: intializing kernel space\n");
    MmSpace_t* space = MmGetKernelSpace();
    space->endAddr = NEXKE_KERNEL_ADDR_END;
    space->startAddr = NEXKE_KERNEL_ADDR_START;
    space->faultHint = NULL;
    space->numEntries = 0;
    // Create entry covering whole address space
    MmSpaceEntry_t* entry = MmCacheAlloc (mmEntryCache);
    entry->count = kernelObj->count;
    entry->obj = kernelObj;
    entry->vaddr = space->startAddr;
    entry->next = NULL;
    entry->prev = NULL;
    space->entryList = entry;
}

// Gets active address space
MmSpace_t* MmGetCurrentSpace()
{
    return mmCurSpace;
}

// Dumps address space
void MmDumpSpace (MmSpace_t* as)
{
    NkSpinLock (&as->lock);
    MmSpaceEntry_t* entry = as->entryList;
    while (entry)
    {
        NkLogDebug ("Found address space entry: base %#llX, page count %d\n",
                    (uint64_t) entry->vaddr,
                    entry->count);
        entry = entry->next;
    }
    NkSpinUnlock (&as->lock);
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
    mmSpaceCache = MmCacheCreate (sizeof (MmSpace_t), "MmSpace_t", 0, 0);
    mmEntryCache = MmCacheCreate (sizeof (MmSpaceEntry_t), "MmSpaceEntry_t", 0, 0);
    if (!mmSpaceCache || !mmEntryCache)
        NkPanicOom();
    mmCurSpace = MmGetKernelSpace();
    // Set up MUL
    MmMulInit();
    // Second phase of KVM
    MmInitKvm2();
}

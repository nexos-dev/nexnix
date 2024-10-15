/*
    page.c - contains page frame manager / allocator
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
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ZONES 1024

static MmZone_t* mmZones[MAX_ZONES] = {0};    // Array of zones
static size_t mmNumZones = 0;                 // Number of zones
static SlabCache_t* mmZoneCache = NULL;       // Slab cache of zones

static void* pfnMapMark = (void*) NEXKE_PFNMAP_BASE;    // Next place to put zone's PFN map at

static SlabCache_t* mmFakePageCache = NULL;    // Fake page cache

static MmZone_t* freeHint = NULL;    // Free zone hint

static SlabCache_t* mmPageMapCache = NULL;    // Cache for page maps

// Page hash table
static NkList_t mmPageHash[MM_MAX_BUCKETS] = {0};

// Informational variables
static uintmax_t mmNumPages = 0;     // Number of pages in system
static uintmax_t mmFreePages = 0;    // Number of free pages in system

// Initializes an MmPage
static void mmInitPage (MmPage_t* page, pfn_t pfn, MmZone_t* zone)
{
    page->zone = zone;
    page->pfn = pfn;
    page->flags = MM_PAGE_FREE;
    page->link.prev = NULL, page->link.next = NULL;
    NkListAddFront (&zone->freeList, &page->link);
    page->maps = NULL;
}

// Checks for overlap between two zones
static inline bool mmZonesOverlap (MmZone_t* z1, MmZone_t* z2)
{
    if ((((z1->pfn + z1->numPages) > z2->pfn) && (z1->pfn < z2->pfn)) ||
        (((z2->pfn + z2->numPages) > z1->pfn) && (z2->pfn < z1->pfn)))
        return true;
    if ((z1->pfn > z2->pfn) && ((z1->pfn + z1->numPages) < (z2->pfn + z2->numPages)) ||
        (z2->pfn > z1->pfn) && ((z2->pfn + z2->numPages) < (z1->pfn + z1->numPages)))
        return true;
    return false;
}

// Inserts zone into zone list
static inline bool mmZoneInsert (MmZone_t* zone)
{
    if (mmNumZones >= MAX_ZONES)
    {
        NkLogWarning ("nexke: ignoring zones past limit MAX_ZONES\n");
        return false;
    }
    // We do a sorted insert, so that all zones are contiguous
    // Loop through each zone and find appropriate point to insert at
    size_t zoneIdx = 0;
    for (int i = 0; i < mmNumZones; ++i)
    {
        if (mmZonesOverlap (mmZones[i], zone))
        {
            NkLogDebug ("nexke: Overlapping memory regions in memory map, z1 starts at %#lX, ends "
                        "at %#lX; z2 starts %#lX, ends at %#lX\n",
                        mmZones[i]->pfn,
                        mmZones[i]->pfn + mmZones[i]->numPages,
                        zone->pfn,
                        zone->pfn + zone->numPages);
            return false;
        }
        // Check if this is the right spot
        if (zone->pfn < mmZones[i]->pfn)
        {
            // This is the correct spot
            // Move everything up one
            for (int j = mmNumZones; j > i; --j)
            {
                mmZones[j] = mmZones[j - 1];
                mmZones[j]->zoneIdx++;
            }
            zoneIdx = i;
            break;
        }
        else
            zoneIdx = i + 1;
    }
    mmZones[zoneIdx] = zone;    // Insert it
    zone->zoneIdx = zoneIdx;
    ++mmNumZones;
    return true;
}

// Removes zone from zone list
static inline void mmZoneRemove (MmZone_t* zone)
{
    // Grab index
    size_t idx = zone->zoneIdx;
    // Move all zones down 1
    for (int i = (idx + 1); i < mmNumZones; ++i)
    {
        mmZones[i]->zoneIdx--;
        mmZones[i - 1] = mmZones[i];
    }
    --mmNumZones;
    MmCacheFree (mmZoneCache, zone);
}

// Merges z1 and z2 into one zone in array
// NOTE: this can only be called during initialization
static bool mmZoneMerge (MmZone_t* z1, MmZone_t* z2)
{
    // Ensure z1 and z2 are mergeable
    if (((z1->pfn + z1->numPages) == z2->pfn) && (z1->flags == z2->flags))
    {
        if (z1->flags & MM_ZONE_ALLOCATABLE)
            assert (z1->freeCount == z1->numPages && z2->freeCount == z2->numPages);
        z1->numPages += z2->numPages;
        z1->freeCount += z2->freeCount;
        // Remove the zones
        mmZoneRemove (z2);
        return true;
    }
    return false;
}

// Splits zone into 2 zones at specified point
static void mmZoneSplit (MmZone_t* zone, pfn_t splitPoint, int newFlags)
{
    assert (zone->freeCount == zone->numPages);
    assert ((splitPoint - zone->pfn) <= zone->numPages);
    MmZone_t* newZone = (MmZone_t*) MmCacheAlloc (mmZoneCache);
    assert (newZone);
    newZone->flags = newFlags;
    // Split the zones
    newZone->pfn = zone->pfn + splitPoint;
    newZone->numPages = (zone->pfn + zone->numPages) - splitPoint;
    zone->numPages -= newZone->numPages;
    // Insert the new zone into the list
    mmZoneInsert (zone);
}

// Creates a zone
static void mmZoneCreate (pfn_t startPfn, size_t numPfns, int flags)
{
    // Create zone
    MmZone_t* zone = (MmZone_t*) MmCacheAlloc (mmZoneCache);
    assert (zone);
    // Initialize zone
    zone->flags = flags;
    zone->numPages = numPfns;
    zone->pfn = startPfn;
    NkListInit (&zone->freeList);
    if (zone->flags & MM_ZONE_ALLOCATABLE)
    {
        // Compute PFN map location
        zone->pfnMap = (MmPage_t*) pfnMapMark;
        // Move marker up
        pfnMapMark += zone->numPages * sizeof (MmPage_t);
        // Initialize PFN structs
        for (int i = 0; i < zone->numPages; ++i)
            mmInitPage (&zone->pfnMap[i], startPfn + i, zone);
        zone->freeCount = zone->numPages;
        // Update state variables
        mmNumPages += zone->numPages;
        mmFreePages += zone->numPages;
    }
    else
    {
        zone->pfnMap = NULL;
        zone->freeCount = 0;
    }
    // Insert it
    if (!mmZoneInsert (zone))
        NkLogWarning ("nexke: warning: ignoring overlapping memory region\n");
}

// Checks if zone will work for allocation
static bool mmZoneWillWork (MmZone_t* zone, pfn_t maxAddr, size_t needed, int bannedFlags)
{
    // Check flags and address
    if (zone->flags & bannedFlags || !(zone->flags & MM_ZONE_ALLOCATABLE))
        return false;
    // Check if zone spans above max address
    if ((zone->pfn + zone->numPages) > maxAddr)
        return false;
    // Ensure zone has free memory
    if (zone->freeCount < needed)
        return false;
    return true;
}

// Finds best zone for allocation, given a set of requirements
static MmZone_t* mmZoneFindBest (pfn_t maxAddr, size_t count, int bannedFlags)
{
    if (!maxAddr)
        maxAddr = -1;
    if (mmZoneWillWork (freeHint, maxAddr, count, bannedFlags))
        return freeHint;
    // Before iterating through zones, try checking zone hint
    for (int i = 0; i < mmNumZones; ++i)
    {
        if (mmZoneWillWork (mmZones[i], maxAddr, count, bannedFlags))
            return mmZones[i];
    }
    return NULL;    // No zone found
}

// Finds zone that contains specified PFN
static MmZone_t* mmZoneFindByPfn (pfn_t pfn)
{
    for (int i = 0; i < mmNumZones; ++i)
    {
        if (mmZones[i]->pfn <= pfn && (mmZones[i]->pfn + mmZones[i]->numPages) > pfn)
            return mmZones[i];
    }
    return NULL;
}

// Frees an MmPage
void MmFreePage (MmPage_t* page)
{
    // Don't free an unusable page
    if (page->flags & MM_PAGE_UNUSABLE && !page->zone)
        MmCacheFree (mmFakePageCache, page);
    else
    {
        MmZone_t* zone = page->zone;
        // Add to free list
        NkListAddFront (&zone->freeList, &page->link);
        // Update stats
        ++zone->freeCount;
        ++mmFreePages;
        page->flags = MM_PAGE_FREE;
    }
}

// Allocates an MmPage, with specified characteristics
// Returns NULL if physical memory is exhausted
MmPage_t* MmAllocPage()
{
    // Ensure we allocate from generic memory zone
    MmZone_t* zone = mmZoneFindBest (0, 1, MM_ZONE_NO_GENERIC);
    if (!zone)
    {
        NkLogDebug ("nexke: warning: potential OOM detected\n");
        return NULL;    // Uh oh
    }
    // Grab a page from the free list
    NkLink_t* link = NkListPopFront (&zone->freeList);
    assert (link);
    MmPage_t* page = LINK_CONTAINER (link, MmPage_t, link);
    // Update state fields
    --zone->freeCount;
    --mmFreePages;
    page->flags = MM_PAGE_ALLOCED;
    return page;    // Return this page
}

// Finds/creates page structure at specified PFN
MmPage_t* MmFindPagePfn (pfn_t pfn)
{
    // Find zone with this page
    MmZone_t* zone = mmZoneFindByPfn (pfn);
    if (zone && zone->flags & MM_ZONE_ALLOCATABLE)
    {
        // Grab from PFN map
        MmPage_t* map = zone->pfnMap;
        // Convert PFN into a PFN relative to base of zone map
        MmPage_t* page = map + (pfn - zone->pfn);
        assert (page->pfn == pfn);
        return page;
    }
    // Else, we need to just forge a fake page
    MmPage_t* page = MmCacheAlloc (mmFakePageCache);
    if (!page)
        NkPanic ("nexke: out of memory\n");
    memset (page, 0, sizeof (MmPage_t));
    page->flags = MM_PAGE_UNUSABLE;
    page->pfn = pfn;
    return page;
}

// Allocates a contigious range of PFNs with specified at limit, beneath specified base adress
MmPage_t* MmAllocPagesAt (size_t count, paddr_t maxAddr, paddr_t align)
{
    // Find zone
    MmZone_t* zone = mmZoneFindBest (maxAddr / NEXKE_CPU_PAGESZ, count, 0);
    if (!zone)
        return NULL;    // Couldn't find page
    // First get to start align
    MmPage_t* pfnMap = zone->pfnMap;
    size_t pfnAlign = align / NEXKE_CPU_PAGESZ;
    while (pfnMap->pfn % pfnAlign)
        ++pfnMap;
    // Attempt to find contigous range of PFNs
    for (int i = 0; i < zone->numPages; i += pfnAlign)
    {
        if (pfnMap[i].flags & MM_PAGE_FREE)
        {
            MmPage_t* firstPage = &pfnMap[i];
            // Find count more
            for (int j = 0; j < count; ++j, ++i)
            {
                if (!(pfnMap[i].flags & MM_PAGE_FREE))
                    break;    // Not a contigous run
            }
            // Found run, remove from list
            for (int j = 0; j < count; ++j)
            {
                MmPage_t* page = &firstPage[j];
                NkListRemove (&zone->freeList, &page->link);
                page->flags = MM_PAGE_ALLOCED;
            }
            // Update stats
            zone->freeCount -= count;
            mmFreePages -= count;
            return firstPage;
        }
    }
    return NULL;    // Shouldn't happen, maybe we should panic
}

// Frees pages allocated with AllocPageAt
void MmFreePages (MmPage_t* pages, size_t count)
{
    for (int i = 0; i < count; ++i)
        MmFreePage (&pages[i]);
}

#define MM_GET_BUCKET(obj, off) ((CpuPageAlignDown ((uintptr_t) obj) + off) % MM_MAX_BUCKETS)

// Adds a page to a page hash list
void MmAddPage (MmObject_t* obj, size_t off, MmPage_t* page)
{
    // Find bucket
    size_t bucket = MM_GET_BUCKET (obj, off);
    // Add to it
    NkListAddFront (&mmPageHash[bucket], &page->link);
    // Set object/offset
    page->offset = off;
    page->obj = obj;
    page->flags |= MM_PAGE_IN_OBJECT;
    // Add to object
    NkListAddFront (&obj->pageList, &page->objLink);
}

// Looks up page in page list, returning NULL if none is found
MmPage_t* MmLookupPage (MmObject_t* obj, size_t off)
{
    // Find bucket
    size_t bucket = MM_GET_BUCKET (obj, off);
    // Find in bucket
    NkLink_t* iter = NkListFront (&mmPageHash[bucket]);
    while (iter)
    {
        MmPage_t* curPage = LINK_CONTAINER (iter, MmPage_t, link);
        if (curPage->offset == off && curPage->obj == obj)
            return curPage;
        iter = NkListIterate (iter);
    }
    return NULL;
}

// Removes a page from the specified hash list
void MmRemovePage (MmPage_t* page)
{
    // Find bucket
    size_t bucket = MM_GET_BUCKET (page->obj, page->offset);
    // Remove from bucket
    NkListRemove (&mmPageHash[bucket], &page->link);
    // Remove from object
    NkListRemove (&page->obj->pageList, &page->objLink);
    page->offset = 0;    // For error checking
    page->obj = NULL;
    page->flags |= MM_PAGE_ALLOCED;    // Page is no longer in object but not free
    page->flags &= ~(MM_PAGE_IN_OBJECT);
}

// Adds mapping to page
void MmPageAddMap (MmPage_t* page, MmSpace_t* space, uintptr_t addr)
{
    // Allocate page map
    MmPageMap_t* map = MmCacheAlloc (mmPageMapCache);
    map->addr = addr;
    map->space = space;
    map->next = page->maps;
    page->maps = map;
}

// Clears mappings from page
void MmPageClearMaps (MmPage_t* page)
{
    MmPageMap_t* map = page->maps;
    while (map)
    {
        MmMulUnmapPage (map->space, map->addr);
        MmPageMap_t* tmp = map;
        map = map->next;
        MmCacheFree (mmPageMapCache, tmp);
    }
    page->maps = NULL;
}

static char* zonesFlags[] = {"MM_ZONE_KERNEL ",
                             "MM_ZONE_MMIO ",
                             "MM_ZONE_RESVD ",
                             "MM_ZONE_RECLAIM ",
                             "MM_ZONE_ALLOCATABLE "};

static inline size_t appendFlag (char* s, const char* flg)
{
    size_t flgLen = strlen (flg);
    memcpy (s, flg, flgLen);
    return flgLen;
}

// Initialize page layer
void MmInitPage()
{
    // Grab bootinfo so we can read memory map
    NexNixBoot_t* boot = NkGetBootArgs();
    // Step 1: go through memory map and figure out
    // 1: the number of zones we need, and 2: the number of PFNs that
    // must be represented
    NbMemEntry_t* memMap = boot->memMap;
    size_t mapSize = boot->mapSize;
    size_t numPfns = 0;
    size_t lastMapEnt = mapSize - 1;
    for (int i = 0; i < mapSize; ++i)
    {
        // Don't include reserved regions
        if (!memMap[i].sz ||
            !(memMap[i].type == NEXBOOT_MEM_FREE || memMap[i].type == NEXBOOT_MEM_FW_RECLAIM ||
              memMap[i].type == NEXBOOT_MEM_BOOT_RECLAIM))
        {
            continue;
        }
        // Figure out number of PFNs to represent
        numPfns += (memMap[i].sz + (NEXKE_CPU_PAGESZ - 1)) / NEXKE_CPU_PAGESZ;
        // Figure out we exceeded max supported PFNs
        if (numPfns >= (NEXKE_PFNMAP_MAX / sizeof (MmPage_t)))
        {
            numPfns = NEXKE_PFNMAP_MAX / sizeof (MmPage_t);
            lastMapEnt = i;
            break;
        }
        // Ensure we don't go over max memory
#ifdef NEXKE_MAX_PAGES
        if (numPfns >= NEXKE_MAX_PAGES)
        {
            numPfns = NEXKE_PFNMAP_MAX / sizeof (MmPage_t);
            lastMapEnt = i;
            break;
        }
#endif
    }
    // Step 2: allocate space for PFN map
    // We will allocate the zone structures from the slab, however, since
    // the amount of memory the slab has to work with is limited right now,
    // we will grab memory for the page structures straight from the memory map
    void* pageMap = NULL;
    // Find a contigous region of memory for PFN map
    for (int i = 0; i < lastMapEnt; ++i)
    {
        if (memMap[i].type == NEXBOOT_MEM_FREE || memMap[i].type == NEXBOOT_MEM_FW_RECLAIM ||
            memMap[i].type == NEXBOOT_MEM_BOOT_RECLAIM)
        {
            // Determine if there is enough space
            if (memMap[i].sz > (numPfns * sizeof (MmPage_t)))
            {
                // Decrease available space
                memMap[i].sz -= CpuPageAlignUp (numPfns * sizeof (MmPage_t));
                // Determine our base address
                paddr_t mapPhys = memMap[i].base + memMap[i].sz;
                // Map it
                size_t numPfnPages =
                    ((numPfns * sizeof (MmPage_t)) + (NEXKE_CPU_PAGESZ - 1)) / NEXKE_CPU_PAGESZ;
                for (int i = 0; i < numPfnPages; ++i)
                {
                    MmMulMapEarly (NEXKE_PFNMAP_BASE + (i * NEXKE_CPU_PAGESZ),
                                   mapPhys + (i * NEXKE_CPU_PAGESZ),
                                   MUL_PAGE_RW | MUL_PAGE_R | MUL_PAGE_KE);
                }
                NkLogDebug ("nexke: Allocating PFN map from %#llX to %#llX\n",
                            (uint64_t) mapPhys,
                            (uint64_t) mapPhys + (numPfnPages * NEXKE_CPU_PAGESZ));
                break;
            }
        }
    }
    // We have now created the PFN map
    // Now we need to initialize the zones
    mmZoneCache = MmCacheCreate (sizeof (MmZone_t), "MmZone_t", 0, 0);
    for (int i = 0; i < lastMapEnt; ++i)
    {
        if (!memMap[i].sz)
            continue;
        // Figure out flags
        int flags = 0;
        if (memMap[i].type == NEXBOOT_MEM_RESVD)
            flags |= MM_ZONE_RESVD;
        else if (memMap[i].type == NEXBOOT_MEM_MMIO)
            flags |= MM_ZONE_MMIO;
        else if (memMap[i].type == NEXBOOT_MEM_ACPI_NVS)
            flags |= MM_ZONE_RESVD;
        else if (memMap[i].type == NEXBOOT_MEM_ACPI_RECLAIM)
            flags |= MM_ZONE_RECLAIM;
        else
            flags |= MM_ZONE_ALLOCATABLE;
        // Create zone
        mmZoneCreate (memMap[i].base / NEXKE_CPU_PAGESZ, memMap[i].sz / NEXKE_CPU_PAGESZ, flags);
    }
    // Merge all mergable zones
    size_t curZone = 1;
    while (curZone < mmNumZones)
    {
        if (!mmZoneMerge (mmZones[curZone - 1], mmZones[curZone]))
            ++curZone;
    }
#ifdef NEXNIX_BOARD_PC
    // On PC, we have some interesting requirements
    // ISA DMA allocates must be from 0 - 16M; hence, we want to ensure that we have a memory zone
    // for that Also, some hardware requires 32 bit physical address, so we want to keep everything
    // beneath 4G in it's own zone
    // Find zone with memory from 0-16M
    for (int i = 0; i < mmNumZones; ++i)
    {
#define MM_16M_END 0x1000000
        if (((mmZones[i]->pfn + mmZones[i]->numPages) * NEXKE_CPU_PAGESZ) < MM_16M_END &&
            (mmZones[i]->flags & MM_ZONE_ALLOCATABLE))
        {
            // We already have an adequate memory region, ensure general purpose allocations are
            // banned
            mmZones[i]->flags |= MM_ZONE_NO_GENERIC;
            break;
        }
        else if ((mmZones[i]->pfn * NEXKE_CPU_PAGESZ) < MM_16M_END &&
                 (mmZones[i]->flags & MM_ZONE_ALLOCATABLE))
        {
            // Zone starts beneath 16M but juts into 16+, split it
            mmZoneSplit (mmZones[i],
                         (MM_16M_END / NEXKE_CPU_PAGESZ),
                         mmZones[i]->flags | MM_ZONE_NO_GENERIC);
            break;
        }
    }
    // Find zone with memory beneath 4G
    for (int i = 0; i < mmNumZones; ++i)
    {
#define MM_4G_END 0x100000000
        if ((mmZones[i]->pfn * NEXKE_CPU_PAGESZ) < MM_4G_END &&
            ((mmZones[i]->pfn + mmZones[i]->numPages) * NEXKE_CPU_PAGESZ) > MM_4G_END &&
            (mmZones[i]->flags & MM_ZONE_ALLOCATABLE))
        {
            // Zone starts beneath 16M but juts into 16+, split it
            mmZoneSplit (mmZones[i],
                         (MM_4G_END / NEXKE_CPU_PAGESZ),
                         mmZones[i]->flags | MM_ZONE_NO_GENERIC);
            break;
        }
    }
#endif
    NkLogInfo ("nexke: found %lluM of free memory\n",
               (mmNumPages * NEXKE_CPU_PAGESZ) / 1024 / 1024);
    // Log zones and set free hint
    MmZone_t* curBest = NULL;
    for (int i = 0; i < mmNumZones; ++i)
    {
        // Check if this zone is an ideal free zone
        if (!curBest || (mmZones[i]->freeCount > curBest->freeCount &&
                         !(mmZones[i]->flags & MM_ZONE_NO_GENERIC)))
        {
            curBest = mmZones[i];
        }
        char typeS[256] = {0};
        char* s = typeS;
        if (mmZones[i]->flags & MM_ZONE_ALLOCATABLE)
        {
            char* flg = zonesFlags[4];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_MMIO)
        {
            char* flg = zonesFlags[1];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_RESVD)
        {
            char* flg = zonesFlags[2];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_RECLAIM)
        {
            char* flg = zonesFlags[3];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_KERNEL)
        {
            char* flg = zonesFlags[0];
            s += appendFlag (s, flg);
        }
        NkLogDebug ("nexke: Found memory region from %#llX to %#llX, flags %s\n",
                    (uintmax_t) mmZones[i]->pfn * NEXKE_CPU_PAGESZ,
                    (uintmax_t) (mmZones[i]->pfn + mmZones[i]->numPages) * NEXKE_CPU_PAGESZ,
                    typeS);
    }
    freeHint = curBest;
    // Create fake page cache
    mmFakePageCache = MmCacheCreate (sizeof (MmPage_t), "MmPage_t", 0, 0);
    assert (mmFakePageCache);
    // Create page map cache
    mmPageMapCache = MmCacheCreate (sizeof (MmPageMap_t), "MmPageMap_t", 0, 0);
    assert (mmPageMapCache);
}

// Dumps out page debugging info
void MmDumpPageInfo()
{
    NkLogDebug ("Page stats:\n");
    // Dump zones
    for (int i = 0; i < mmNumZones; ++i)
    {
        char typeS[256] = {0};
        char* s = typeS;
        if (mmZones[i]->flags & MM_ZONE_ALLOCATABLE)
        {
            char* flg = zonesFlags[4];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_MMIO)
        {
            char* flg = zonesFlags[1];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_RESVD)
        {
            char* flg = zonesFlags[2];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_RECLAIM)
        {
            char* flg = zonesFlags[3];
            s += appendFlag (s, flg);
        }
        if (mmZones[i]->flags & MM_ZONE_KERNEL)
        {
            char* flg = zonesFlags[0];
            s += appendFlag (s, flg);
        }
        NkLogDebug ("Zone %d: physical base = %p, end = %p, free page count = %u, flags %s, is "
                    "free hint = %s\n",
                    mmZones[i]->zoneIdx,
                    mmZones[i]->pfn * NEXKE_CPU_PAGESZ,
                    (mmZones[i]->pfn + mmZones[i]->numPages) * NEXKE_CPU_PAGESZ,
                    mmZones[i]->freeCount,
                    typeS,
                    mmZones[i] == freeHint ? "true" : "false");
    }
    // Dump variables
    NkLogDebug ("Total number of pages: %llu\n", mmNumPages);
    NkLogDebug ("Total number of free pages: %llu\n", mmFreePages);
}

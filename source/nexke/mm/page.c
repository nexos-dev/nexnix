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

#define MAX_ZONES 256

static MmZone_t* zones[MAX_ZONES + 1] = {0};    // Array of zones
static size_t numZones = 0;                     // Number of zones
static SlabCache_t* zoneCache = NULL;           // Slab cache of zones

// Initializes an MmPage
static void mmInitPage (MmPage_t* page, pfn_t pfn, MmZone_t* zone)
{
    page->zone = zone;
    page->pfn = pfn;
    page->state = MM_PAGE_STATE_FREE;
    page->prev = NULL;
    page->refCount = 0;
    page->next = zone->freeList;
    zone->freeList = page;
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
    if (numZones >= MAX_ZONES)
    {
        NkLogWarning ("nexke: ignoring zones past limit MAX_ZONES\n");
        return false;
    }
    // We do a sorted insert, so that all zones are contiguous
    // Loop through each zone and find appropriate point to insert at
    size_t zoneIdx = 0;
    for (int i = 0; i < numZones; ++i)
    {
        if (mmZonesOverlap (zones[i], zone))
        {
            NkLogDebug ("nexke: Overlapping memory regions in memory map, z1 starts at %#lX, ends "
                        "at %#lX; z2 starts %#lX, ends at %#lX\n",
                        zones[i]->pfn,
                        zones[i]->pfn + zones[i]->numPages,
                        zone->pfn,
                        zone->pfn + zone->numPages);
            return false;
        }
        // Check if this is the right spot
        if (zone->pfn < zones[i]->pfn)
        {
            // This is the correct spot
            // Move everything up one
            for (int j = numZones; j > i; --j)
            {
                zones[j] = zones[j - 1];
                zones[j]->zoneIdx++;
            }
            zoneIdx = i;
            break;
        }
        else
            zoneIdx = i + 1;
    }
    zones[zoneIdx] = zone;    // Insert it
    zone->zoneIdx = zoneIdx;
    ++numZones;
    return true;
}

// Removes zone from zone list
static inline void mmZoneRemove (MmZone_t* zone)
{
    // Grab index
    size_t idx = zone->zoneIdx;
    // Move all zones down 1
    for (int i = (idx + 1); i < numZones; ++i)
    {
        zones[i]->zoneIdx--;
        zones[i - 1] = zones[i];
    }
    --numZones;
}

// Merges z1 and z2 into one zone in array
// NOTE: this can only be called during initialization
static bool mmZoneMerge (MmZone_t* z1, MmZone_t* z2)
{
    // Ensure z1 and z2 are mergeable
    if (((z1->pfn + z1->numPages) == z2->pfn) && (z1->flags == z2->flags))
    {
        assert (z2->freeCount == z2->numPages);
        z1->numPages += z2->numPages;
        z1->freeCount += z2->freeCount;
        // Remove the zones
        mmZoneRemove (z2);
        return true;
    }
    return false;
}

// Splits zone into 2 zones at specified point
static void mmZoneSplit (size_t splitPoint, int newFlags)
{
}

// Creates a zone
static void mmZoneCreate (pfn_t startPfn, size_t numPfns, int flags)
{
    // Create zone
    MmZone_t* zone = (MmZone_t*) MmCacheAlloc (zoneCache);
    assert (zone);
    // Initialize zone
    zone->flags = flags;
    zone->numPages = numPfns;
    zone->pfn = startPfn;
    // Compute PFN map location
    zone->pfnMap = (MmPage_t*) NEXKE_PFNMAP_BASE + startPfn;
    // Initialize PFN structs
    for (int i = 0; i < zone->numPages; ++i)
        mmInitPage (zone->pfnMap[i]);
    if (zone->flags & MM_ZONE_ALLOCATABLE)
    {
        zone->freeCount = zone->numPages;
        zone->freeList = NULL;
    }
    // Insert it
    if (!mmZoneInsert (zone))
        NkLogWarning ("nexke: warning: ignoring overlapping memory region\n");
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
        if (!memMap[i].sz)
            continue;
        // Figure out wheter we need a PFN map or not
        if (memMap[i].type == NEXBOOT_MEM_ACPI_NVS || memMap[i].type == NEXBOOT_MEM_RESVD)
            continue;    // No PFN map needed
        // Else, we do need a PFN map; figure out number of PFNs to represent
        numPfns += memMap[i].sz / NEXKE_CPU_PAGESZ;
        // Figure out we exceeded max supported PFNs
        if (numPfns >= (NEXKE_PFNMAP_MAX / sizeof (MmPage_t)))
        {
            numPfns = NEXKE_PFNMAP_MAX / sizeof (MmPage_t);
            lastMapEnt = i;
            break;
        }
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
                memMap[i].sz -= CpuPageAlignDown (numPfns * sizeof (MmPage_t));
                numPfns -= ((numPfns * sizeof (MmPage_t)) / NEXKE_CPU_PAGESZ);
                // Determine our base address
                void* mapPhys = (void*) (memMap[i].base + memMap[i].sz);
                // Map it
                size_t numPfnPages = (numPfns * sizeof (MmPage_t) / NEXKE_CPU_PAGESZ);
                for (int i = 0; i < numPfnPages; ++i)
                {
                    MmMulMapEarly (NEXKE_PFNMAP_BASE + (i * NEXKE_CPU_PAGESZ),
                                   (paddr_t) mapPhys + (i * NEXKE_CPU_PAGESZ),
                                   MUL_PAGE_RW | MUL_PAGE_R | MUL_PAGE_KE);
                }
                NkLogDebug ("nexke: Allocating PFN map from %p to %p\n",
                            mapPhys,
                            mapPhys + (numPfnPages * NEXKE_CPU_PAGESZ));
                break;
            }
        }
    }
    // We have now created the PFN map
    // Now we need to initialize the zones
    zoneCache = MmCacheCreate (sizeof (MmZone_t), NULL, NULL);
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
    // Copy zones into temporary array
    MmZone_t** tmpZones = malloc (numZones * sizeof (MmZone_t*));
    assert (tmpZones);
    memcpy (tmpZones, zones, numZones * sizeof (MmZone_t*));
    size_t numZonesTmp = numZones;
    for (int i = 1; i < numZonesTmp; ++i)
    {
        mmZoneMerge (tmpZones[i - 1], tmpZones[i]);
    }
    free (tmpZones);
    // Log zones
    for (int i = 0; i < numZones; ++i)
    {
        char typeS[256] = {0};
        char* s = typeS;
        if (zones[i]->flags & MM_ZONE_ALLOCATABLE)
        {
            char* flg = zonesFlags[4];
            s += appendFlag (s, flg);
        }
        if (zones[i]->flags & MM_ZONE_MMIO)
        {
            char* flg = zonesFlags[1];
            s += appendFlag (s, flg);
        }
        if (zones[i]->flags & MM_ZONE_RESVD)
        {
            char* flg = zonesFlags[2];
            s += appendFlag (s, flg);
        }
        if (zones[i]->flags & MM_ZONE_RECLAIM)
        {
            char* flg = zonesFlags[3];
            s += appendFlag (s, flg);
        }
        if (zones[i]->flags & MM_ZONE_KERNEL)
        {
            char* flg = zonesFlags[0];
            s += appendFlag (s, flg);
        }
        NkLogDebug ("page: Found memory region from %p to %p, flags %s\n",
                    zones[i]->pfn * NEXKE_CPU_PAGESZ,
                    (zones[i]->pfn + zones[i]->numPages) * NEXKE_CPU_PAGESZ,
                    typeS);
    }
}

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

#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>

static MmZone_t* zones = NULL;    // Array of zones
static SlabCache_t* zoneCache = NULL;

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
    size_t numZones = 0;
    size_t numPfns = 0;
    size_t lastMapEnt = mapSize - 1;
    for (int i = 0; i < mapSize; ++i)
    {
        if (!memMap[i].sz)
            continue;
        ++numZones;
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
    zoneCache = MmCacheCreate (sizeof (MmZone_t), NULL, NULL);
    void* pageMap = NULL;
    // Find a contigous region of memory for PFN map
    for (int i = 0; i < mapSize; ++i)
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
                break;
            }
        }
    }
}

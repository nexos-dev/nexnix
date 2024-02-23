/*
    efimem.c - contains EFI memory map functions
    Copyright 2023 - 2024 The NexNix Project

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

#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <string.h>

// Current map key
static uint32_t mapKey = 0;

// Performs memory detection
void NbFwMemDetect()
{
    // On EFI, this function is a no-op. Memory detection is performed in
    // NbGetMemMap
}

// Returns memory map
NbMemEntry_t* NbGetMemMap (int* size)
{
    // Determine size needed for memory map
    UINTN mapSize = 0;
    UINTN descSz = 0;
    uint32_t descVer = 0;
    uint8_t dummyMap[2] = {0};
    if (BS->GetMemoryMap (&mapSize,
                          (EFI_MEMORY_DESCRIPTOR*) dummyMap,
                          (UINTN*) &mapKey,
                          &descSz,
                          &descVer) != EFI_BUFFER_TOO_SMALL)
    {
        return NULL;
    }
    // Allocate buffer for map
    mapSize += 1024;    // In case map size changes
    void* memMap = NbEfiAllocPool (mapSize);
    if (!memMap)
        return NULL;
    // Zero it
    memset (memMap, 0, mapSize);
    // Now get the memory map
    EFI_STATUS status;
    if ((status = BS->GetMemoryMap (&mapSize, memMap, (UINTN*) &mapKey, &descSz, &descVer)) !=
        EFI_SUCCESS)
    {
        NbEfiFreePool (memMap);
        return NULL;
    }
    // Compute size of map in NbMemEntry_t
    size_t numEntry = mapSize / sizeof (EFI_MEMORY_DESCRIPTOR);
    // Allocate actual map
    NbMemEntry_t* map = (NbMemEntry_t*) NbEfiAllocPool (numEntry * sizeof (NbMemEntry_t));
    if (!map)
    {
        NbEfiFreePool (memMap);
        return NULL;
    }
    memset (map, 0, numEntry * sizeof (NbMemEntry_t));
    // Convert
    int maxEntry = numEntry;
    uint64_t sz = 0;
    for (int i = 0; i < numEntry; ++i)
    {
        EFI_MEMORY_DESCRIPTOR* desc = (void*) memMap + (i * descSz);
        // Check if this is end
        if (desc->PhysicalStart == 0 && desc->NumberOfPages == 0 && desc->Type == 0)
        {
            maxEntry = i - 1;    // Don't include this entry
            break;
        }
        // Copy fields
        map[i].base = desc->PhysicalStart;
        map[i].sz = desc->NumberOfPages * NEXBOOT_CPU_PAGE_SIZE;
        // Convert type
        if (desc->Type == EfiConventionalMemory)
            map[i].type = NEXBOOT_MEM_FREE;
        else if (desc->Type == EfiLoaderCode || desc->Type == EfiLoaderData)
            map[i].type = NEXBOOT_MEM_BOOT_RECLAIM;
        else if (desc->Type == EfiBootServicesCode || desc->Type == EfiBootServicesData)
            map[i].type = NEXBOOT_MEM_FW_RECLAIM;
        else if (desc->Type == EfiMemoryMappedIO || desc->Type == EfiMemoryMappedIOPortSpace)
            map[i].type = NEXBOOT_MEM_MMIO;
        else if (desc->Type == EfiACPIReclaimMemory)
            map[i].type = NEXBOOT_MEM_ACPI_RECLAIM;
        else if (desc->Type == EfiACPIMemoryNVS)
            map[i].type = NEXBOOT_MEM_ACPI_NVS;
        else
            map[i].type = NEXBOOT_MEM_RESVD;
        map[i].flags = 0;
    }
    // EFI memory map is no longer needed
    NbEfiFreePool (memMap);
    *size = maxEntry;
    return map;
}

// Gets map key
uint32_t NbEfiGetMapKey()
{
    return mapKey;
}

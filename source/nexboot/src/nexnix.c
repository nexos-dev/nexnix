/*
    nexnix.c - contains NexNix boot type booting function
    Copyright 2023 The NexNix Project

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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/os.h>
#include <nexboot/shell.h>
#include <string.h>

#define NEXBOOT_MOD_MAX    32
#define NEXBOOT_MEMPOOL_SZ (32 * 1024)    // 32 KiB

// NexNix boot structure
typedef struct _nnboot
{
    // System hardware info
    char sysName[256];         // Sysinfo name
    uint32_t detectedComps;    // Detected architecture components
    uintptr_t comps[32];       // Component table pointers
                               // NOTE: some have no table and only BIOS ints

    uint8_t fw;    // Firmware type we booted from
    // Log info
    uintptr_t logBase;    // Base address of log
    // Memory map
    NbMemEntry_t* memMap;    // Memory map
    int mapSize;             // Entries in memory map
    // Modules info
    void* mods[NEXBOOT_MOD_MAX];    // Loaded modules bases
    int numMods;                    // Number of loaded modules
    // Early memory pool
    void* memPool;      // Early memory pool
    int memPoolSize;    // Size of early memory pool
    // Arguments
    char args[256];    // Command line arguments
} NexNixBoot_t;

// Reads in a file component
void* osReadFile (NbObject_t* fs, const char* name)
{
    NbShellWrite ("Loading %s...\n", name);
    // Read in file
    NbFile_t* file = NbShellOpenFile (fs, name);
    if (!file)
    {
        NbShellWrite ("nexboot: unable to open file \"%s\"\n", name);
        return NULL;
    }
    // Allocate memory for file
    int numPages =
        (file->size + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    void* fileBase = (void*) NbFwAllocPages (numPages);
    if (!fileBase)
    {
        NbShellWrite ("nexboot: out of memory");
        NbCrash();
    }
    // Read in file
    for (int i = 0; i < numPages; ++i)
    {
        if (!NbVfsReadFile (fs,
                            file,
                            fileBase + (i * NEXBOOT_CPU_PAGE_SIZE),
                            NEXBOOT_CPU_PAGE_SIZE))
        {
            NbVfsCloseFile (fs, file);
            NbShellWrite ("nexboot: unable to read file \"%s\"", name);
            return NULL;
        }
    }
    NbVfsCloseFile (fs, file);
    return fileBase;
}

bool NbOsBootNexNix (NbOsInfo_t* info)
{
    // Sanitize input
    if (!info->payload)
    {
        NbShellWrite ("nexboot: error: payload not specified\n");
        return false;
    }
    NbObject_t* fs = NbShellGetRootFs();
    if (!fs)
    {
        NbShellWrite ("nexboot: error: no root filesystem\n");
        return false;
    }
    // Read in kernel file
    void* keFileBase = osReadFile (fs, StrRefGet (info->payload));
    if (!keFileBase)
        return false;
    // Initialize boot info struct
    NexNixBoot_t* bootInfo = malloc (sizeof (NexNixBoot_t));
    memset (bootInfo, 0, sizeof (NexNixBoot_t));
    // Initialize sysinfo
    NbSysInfo_t* sysInf = NbObjGetData (NbObjFind ("/Devices/Sysinfo"));
    bootInfo->fw = sysInf->sysFwType;
    bootInfo->detectedComps = sysInf->detectedComps;
    memcpy (bootInfo->comps, sysInf->comps, 32 * sizeof (uintptr_t));
    strcpy (bootInfo->sysName, sysInf->sysType);
    // Set log base
    bootInfo->logBase = NbLogGetBase();
    // Get memory map
    bootInfo->memMap = NbGetMemMap (&bootInfo->mapSize);
    // Allocate early memory pool
    bootInfo->memPool = (void*) NbFwAllocPages (
        (NEXBOOT_MEMPOOL_SZ + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE);
    if (!bootInfo->memPool)
    {
        NbShellWrite ("nexboot: out of memory");
        NbCrash();
    }
    bootInfo->memPoolSize = NEXBOOT_MEMPOOL_SZ;
    // Load modules
    if (info->mods)
    {
        ArrayIter_t iterSt = {0};
        ArrayIter_t* iter = ArrayIterate (info->mods, &iterSt);
        while (iter)
        {
            // Grab the module path
            StringRef_t** ref = iter->ptr;
            const char* mod = StrRefGet (*ref);
            // Read the file
            bootInfo->mods[bootInfo->numMods] = osReadFile (fs, mod);
            if (!bootInfo->mods[bootInfo->numMods])
            {
                // Error occured
                return false;
            }
            iter = ArrayIterate (info->mods, iter);
        }
    }
    // Copy arguments
    strcpy (bootInfo->args, StrRefGet (info->args));
    // We have now reached that point in loading.
    // It's time to launch the kernel. First, however, we must map the kernel
    // into the address space.
    // Initialize address space manager
    NbCpuAsInit();
    // Map in firmware-dictated memory regions
    NbFwMapRegions();
    // Load up the kernel into memory
#ifdef NEXNIX_ARCH_I386
    uintptr_t entry = NbElfLoadFile (keFileBase);
#endif
    // Call it
    NbCpuLaunchKernel (entry, (uintptr_t) bootInfo);
    return false;
}

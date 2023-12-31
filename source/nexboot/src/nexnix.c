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
#include <nexboot/drivers/display.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/os.h>
#include <nexboot/shell.h>
#include <string.h>

#define NEXBOOT_MOD_MAX    32
#define NEXBOOT_MEMPOOL_SZ (32 * 1024)    // 32 KiB

// NexNix boot structures
#ifdef NEXNIX_ARCH_RISCV64
typedef struct _nncpu
{
    uint64_t misa;
    uint64_t mimpid;
    uint64_t marchid;
    uint64_t mvendorid;
} NexNixCpu_t;
#else
typedef struct _nncpu
{
} NexNixCpu_t;
#endif

// Defined in display.h
/*typedef struct _masksz
{
    uint32_t mask;         // Value to mask component with
    uint32_t maskShift;    // Amount to shift component by
} NbPixelMask_t;*/

typedef struct _nndisplay
{
    int width;           // Width of seleceted mode
    int height;          // Height of selected mode
    int bytesPerLine;    // Bytes per scanline
    char bpp;            // Bits per pixel
    char bytesPerPx;
    size_t lfbSize;
    NbPixelMask_t redMask;    // Masks
    NbPixelMask_t greenMask;
    NbPixelMask_t blueMask;
    NbPixelMask_t resvdMask;
    void* frameBuffer;    // Base of framebuffer
} NexNixDisplay_t;

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
    // Display info
    bool displayDefault;        // If true, display is in same state firmware left it in
    NexNixDisplay_t display;    // Display info
    NexNixCpu_t cpu;            // CPU info
} NexNixBoot_t;

// Reads in a file component
void* osReadFile (NbObject_t* fs, bool persists, const char* name)
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
    int numPages = (file->size + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    void* fileBase = NULL;
    if (persists)
        fileBase = (void*) NbFwAllocPersistentPages (numPages);
    else
        fileBase = (void*) NbFwAllocPages (numPages);
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
    char rootFsBuf[128];
    NbLogMessage ("nexboot: Booting from %s%s using method NexNix\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  NbObjGetPath (fs, rootFsBuf, 128),
                  StrRefGet (info->payload));
    // Read in kernel file
    void* keFileBase = osReadFile (fs, false, StrRefGet (info->payload));
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
    // Allocate early memory pool
    bootInfo->memPool = (void*) NbFwAllocPersistentPages (
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
            bootInfo->mods[bootInfo->numMods] = osReadFile (fs, true, mod);
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
    // Find primary display
    NbObject_t* displayIter = NULL;
    bool foundDisplay = false;
    NbObject_t* devDir = NbObjFind ("/Devices");
    while ((displayIter = NbObjEnumDir (devDir, displayIter)))
    {
        if (displayIter->type == OBJ_TYPE_DEVICE && displayIter->interface == OBJ_INTERFACE_DISPLAY)
        {
            foundDisplay = true;
            break;
        }
    }
    if (foundDisplay)
    {
        NbDisplayDev_t* display = NbObjGetData (displayIter);
        // Copy fields
        bootInfo->displayDefault = false;
        bootInfo->display.width = display->width;
        bootInfo->display.height = display->height;
        bootInfo->display.bytesPerLine = display->bytesPerLine;
        bootInfo->display.bytesPerPx = display->bytesPerPx;
        bootInfo->display.bpp = display->bpp;
        bootInfo->display.lfbSize = display->lfbSize;
        bootInfo->display.frameBuffer = display->frontBuffer;
        memcpy (&bootInfo->display.redMask, &display->redMask, sizeof (NbPixelMask_t));
        memcpy (&bootInfo->display.greenMask, &display->greenMask, sizeof (NbPixelMask_t));
        memcpy (&bootInfo->display.blueMask, &display->blueMask, sizeof (NbPixelMask_t));
        memcpy (&bootInfo->display.resvdMask, &display->resvdMask, sizeof (NbPixelMask_t));
    }
    else
        bootInfo->displayDefault = true;
    // We have now reached that point in loading.
    // It's time to launch the kernel. First, however, we must map the kernel
    // into the address space.
    // Load up the kernel into memory
    uintptr_t entry = NbElfLoadFile (keFileBase);
    // Allocate a boot stack
    uintptr_t stack = NbFwAllocPersistentPage();
    memset ((void*) stack, 0, NEXBOOT_CPU_PAGE_SIZE);
    NbCpuAsMap (NB_KE_STACK_BASE - NEXBOOT_CPU_PAGE_SIZE, stack, NB_CPU_AS_RW | NB_CPU_AS_NX);
    // Map in firmware-dictated memory regions
    NbFwMapRegions (bootInfo->memMap, bootInfo->mapSize);
    // Get memory map
    bootInfo->memMap = NbGetMemMap (&bootInfo->mapSize);
    // Exit from clutches of FW
    NbFwExit();
    // Enable paging
    NbCpuEnablePaging();
    // Launch the kernel
    NbCpuLaunchKernel (entry, (uintptr_t) bootInfo);
    return false;
}

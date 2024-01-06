/*
    nexboot.h - contains bootloader structures
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

#ifndef _NEXBOOT_H
#define _NEXBOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Defines
#define NEXBOOT_MOD_MAX    32
#define NEXBOOT_MEMPOOL_SZ (128 * 1024)    // 128 KiB

// Firmware types
#define NB_FW_TYPE_BIOS 1

// Memory types
#define NEXBOOT_MEM_FREE         1
#define NEXBOOT_MEM_RESVD        2
#define NEXBOOT_MEM_ACPI_RECLAIM 3
#define NEXBOOT_MEM_ACPI_NVS     4
#define NEXBOOT_MEM_MMIO         5
#define NEXBOOT_MEM_FW_RECLAIM   6
#define NEXBOOT_MEM_BOOT_RECLAIM 7

#define NEXBOOT_MEM_FLAG_NON_VOLATILE (1 << 0)

// Memory map abstractions
typedef struct _mementry
{
    uintmax_t base;        // Base of region
    uintmax_t sz;          // Size of region
    unsigned int type;     // Memory type
    unsigned int flags;    // Memory flags
} NbMemEntry_t;

// NexNix boot structures
typedef struct _masksz
{
    uint32_t mask;         // Value to mask component with
    uint32_t maskShift;    // Amount to shift component by
} NbPixelMask_t;

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
} NexNixBoot_t;

// Returns boot arguments
NexNixBoot_t* NkGetBootArgs();

#endif

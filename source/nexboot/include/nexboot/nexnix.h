/*
    nexnix.c - contains NexNix headers
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

#ifndef _NEXNIX_H
#define _NEXNIX_H

#include <nexboot/drivers/display.h>
#include <nexboot/fw.h>

#define NEXBOOT_MOD_MAX    32
#define NEXBOOT_MEMPOOL_SZ (128 * 1024)    // 128 KiB
#ifdef NEXNIX_ARCH_I386
#define NEXBOOT_MEMPOOL_BASE 0xC8000000
#else
#define NEXBOOT_MEMPOOL_BASE 0xFFFFFFFF88000000
#endif

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

#endif

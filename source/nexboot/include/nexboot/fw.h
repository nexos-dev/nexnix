/*
    fw.h - contains firmware related abstractions
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

#ifndef _FW_H
#define _FW_H

#ifdef NEXNIX_FW_BIOS
#include <nexboot/bios/bios.h>
#endif

// Include CPU header
#ifdef NEXNIX_ARCH_I386
#include <nexboot/cpu/i386/cpu.h>
#endif

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

/// Initializes memory map
void NbFwMemDetect();

/// Reads memory map
NbMemEntry_t* NbGetMemMap (int* size);

// Allocates a page for nexboot
uintptr_t NbFwAllocPage();

#endif

/*
    bios.h - contains BIOS calling related functions
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

#ifndef _BIOS_H
#define _BIOS_H

#include <stdint.h>

/// Structure that contains register state for BIOS interrupts
typedef struct _biosregFrame
{
    // These unions allow for individual bytes to be set
    union
    {
        uint32_t eax;
        uint16_t ax;
        struct
        {
            uint8_t al;
            uint8_t ah;
        };
    };
    union
    {
        uint32_t ebx;
        uint16_t bx;
        struct
        {
            uint8_t bl;
            uint8_t bh;
        };
    };
    union
    {
        uint32_t ecx;
        uint16_t cx;
        struct
        {
            uint8_t cl;
            uint8_t ch;
        };
    };
    union
    {
        uint32_t edx;
        uint16_t dx;
        struct
        {
            uint8_t dl;
            uint8_t dh;
        };
    };
    union
    {
        uint32_t esi;
        uint16_t si;
    };
    union
    {
        uint32_t edi;
        uint16_t di;
    };
    uint16_t ds;
    uint16_t es;
    uint16_t flags;
} __attribute__ ((packed)) NbBiosRegs_t;

/// Calls BIOS function with specified register state
void NbBiosCall (uint32_t intNo, NbBiosRegs_t* in, NbBiosRegs_t* out);

/// Calls MBR. Used for chainloading
void NbBiosCallMbr (uint8_t driveNum);

/// Prints a character to screen using int 0x10. Used during early phases
void NbFwEarlyPrint (char c);

// Memory constants
#define NEXBOOT_BIOS_MEMBASE 0x100000
#define NEXBOOT_BIOS_BASE    0x200000

#define NEXBOOT_BIOSBUF_BASE  0xE000
#define NEXBOOT_BIOSBUF2_BASE 0xF000

#define NEXBOOT_BIOS_MBR_BASE 0x7C00

// System detection macros

// PC architecture components
#define NB_ARCH_COMP_ACPI    0
#define NB_ARCH_COMP_MPS     1
#define NB_ARCH_COMP_PNP     2
#define NB_ARCH_COMP_APM     3
#define NB_ARCH_COMP_SMBIOS  4
#define NB_ARCH_COMP_SMBIOS3 5
#define NB_ARCH_COMP_PCI     6
#define NB_ARCH_COMP_VESA    7
#define NB_ARCH_COMP_BIOS32  8
#define NB_ARCH_COMP_TCG_TPM 10

// BIOS disk info structure
typedef struct _biosDisk
{
    NbHwDevice_t hdr;     // Standard header
    uint8_t biosNum;      // BIOS disk number of this drive
    uint8_t flags;        // Disk flags
    int type;             // Media type of disk
    uint64_t size;        // Size of disk in sectors
    uint16_t sectorSz;    // Size of one sector
    uint16_t hpc;         // Heads per cylinder
    uint8_t spt;          // Sectors per track
} NbBiosDisk_t;

#endif

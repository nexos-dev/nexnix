/*
    biosmem.c - contains BIOS memory map functions
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

#include <assert.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Normalized memory map
static NbMemEntry_t memmap[256] = {0};
static int memEntry = 0;

// E820 entry
typedef struct e820ent
{
    uint64_t base;
    uint64_t sz;
    uint32_t type;
    uint32_t resvd;
} __attribute__ ((packed)) nbE820Ent_t;

#define E820_TYPE_FREE         1
#define E820_TYPE_RESVD        2
#define E820_TYPE_ACPI_RECLAIM 3
#define E820_TYPE_ACPI_NVS     4

// Type translation table
static short e820ToNb[] = {0,
                           NEXBOOT_MEM_FREE,
                           NEXBOOT_MEM_RESVD,
                           NEXBOOT_MEM_ACPI_RECLAIM,
                           NEXBOOT_MEM_ACPI_NVS};

bool nbMemWithE820()
{
    nbE820Ent_t* curEntry = (nbE820Ent_t*) NEXBOOT_BIOSBUF_BASE;
    memset (curEntry, 0, sizeof (nbE820Ent_t));
    // Call E820
    NbBiosRegs_t regs = {0};
    uint32_t totalMem = 0;    // Total detected memory
    // Loop to get the rest of the entries
    while (1)
    {
        regs.eax = 0xE820;
        regs.edx = 0x534D4150;
        regs.ecx = 24;
        regs.es = ((uintptr_t) curEntry) >> 4;
        regs.di = ((uintptr_t) curEntry) & 0x0F;
        NbBiosCall (0x15, &regs, &regs);
        // Check results
        if (regs.eax != 0x534D4150)
            return false;
        if ((regs.flags & NEXBOOT_CPU_CARRY_FLAG) == NEXBOOT_CPU_CARRY_FLAG)
            break;
        // Get address
        nbE820Ent_t* desc = (nbE820Ent_t*) NEXBOOT_BIOSBUF_BASE;
        // Page align base and lengths
        if (desc->type == E820_TYPE_FREE || desc->type == E820_TYPE_ACPI_RECLAIM)
        {
            // Round up free base to next page to avoid infringing on a reserved
            // region
            memmap[memEntry].base = NbPageAlignUp (desc->base);
            totalMem += (desc->sz / 1024);
        }
        else if (desc->type == E820_TYPE_RESVD)
        {
            // Round down on reserved regions
            memmap[memEntry].base = NbPageAlignDown (desc->base);
        }
        else
        {
            // On ACPI NVS regions, leave base unaligned
            memmap[memEntry].base = desc->base;
        }
        // Always round length down. We do this because if we have
        // two consecutive reserved regions and we round base down on one
        // and length up on the another, they will overlap by at most PAGE_SIZE -1
        // It's better just not to report this memory
        // This will leave probably upwards of about 12K unusable, however
        memmap[memEntry].sz = NbPageAlignDown (desc->sz);
        if (memmap[memEntry].sz == 0)
        {
            // Round to one page
            memmap[memEntry].sz = NEXBOOT_CPU_PAGE_SIZE;
        }
        memmap[memEntry].type = e820ToNb[desc->type];
        ++memEntry;
        if (!regs.ebx)
            break;
    }
    return true;
}

bool nbMemWithE881()
{
    // Call E881
    NbBiosRegs_t in = {0}, out = {0};
    in.eax = 0xE881;
    in.ecx = 0;
    in.edx = 0;
    NbBiosCall (0x15, &in, &out);
    uint32_t extMem = 0, extMemPlus = 0;
    // Ensure it worked
    if ((out.flags & NEXBOOT_CPU_CARRY_FLAG) == NEXBOOT_CPU_CARRY_FLAG)
    {
        // Try 0xE801
        in.eax = 0xE801;
        in.ecx = 0;
        in.edx = 0;
        NbBiosCall (0x15, &in, &out);
        if ((out.flags & NEXBOOT_CPU_CARRY_FLAG) == NEXBOOT_CPU_CARRY_FLAG)
            return false;
        // Check if result is in EAX/EBX or ECX/EDX
        if (!out.cx && !out.dx)
        {
            extMem = out.ax;
            extMemPlus = out.bx;
        }
        else
        {
            extMem = out.cx;
            extMemPlus = out.dx;
        }
    }
    else
    {
        // Check if result is in EAX/EBX or ECX/EDX
        if (!out.ecx && !out.edx)
        {
            extMem = out.eax;
            extMemPlus = out.ebx;
        }
        else
        {
            extMem = out.ecx;
            extMemPlus = out.edx;
        }
    }
    // Prepare memory entries
    memmap[0].base = 0;
    memmap[0].sz = 0x80000;
    memmap[0].type = NEXBOOT_MEM_FREE;
    memmap[1].base = 0x100000;
    memmap[1].sz = NbPageAlignDown ((uint64_t) extMem * 1024);
    memmap[1].type = NEXBOOT_MEM_FREE;
    memmap[2].base = 0x1000000;
    memmap[2].sz = NbPageAlignDown ((uint64_t) extMemPlus * (1024 * 64));
    memmap[2].type = NEXBOOT_MEM_FREE;
    memEntry = 3;
    return true;
}

bool nbMemWith8A()
{
    NbBiosRegs_t in = {0}, out = {0};
    uint32_t extMemSz = 0;
    in.ah = 0x8A;
    NbBiosCall (0x15, &in, &out);
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
    {
        // Try DA88h
        in.ax = 0xDA88;
        NbBiosCall (0x15, &in, &out);
        if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
            return false;
        // Get memory size
        extMemSz = ((out.cl << 16) | out.bx) * 1024;
    }
    else
    {
        // Get memory size
        extMemSz = ((out.dx << 16) | out.ax) * 1024;
    }
    extMemSz = NbPageAlignDown (extMemSz);
    // Prepare memory entries
    memmap[0].base = 0;
    memmap[0].sz = 0x80000;
    memmap[0].type = NEXBOOT_MEM_FREE;
    // Figure out wheter we must account the ISA memory hole
    if (extMemSz <= 15728640)
    {
        // We have less than 15 MiB of memory, add block up to limit
        memmap[1].base = 0x100000;
        memmap[1].sz = extMemSz;
        memmap[1].type = NEXBOOT_MEM_FREE;
        memEntry = 2;
    }
    // Else we do (probably) have an ISA memory hole
    else
    {
        memmap[1].base = 0x100000;
        memmap[1].sz = 0xE00000;
        memmap[1].type = NEXBOOT_MEM_FREE;
        memmap[2].base = 0x1000000;
        memmap[2].sz = extMemSz - 0xF00000;
        memmap[2].type = NEXBOOT_MEM_FREE;
        memEntry = 3;
    }
    return true;
}

bool nbMemWith88()
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x88;
    NbBiosCall (0x15, &in, &out);
    // NOTE NOTE NOTE: Some BIOSes don't set CF on error for this function.
    // I'm not sure of the best way to handle this; currently, if CF isn't set
    // we check AH
    if ((out.flags & NEXBOOT_CPU_CARRY_FLAG) || out.ah == 0x80 || out.ah == 0x86)
        return false;
    // Get memory size
    uintmax_t memSize = out.ax;
    // Prepare memory entries
    memmap[0].base = 0;
    memmap[0].sz = 0xA0000;
    memmap[0].type = NEXBOOT_MEM_FREE;
    memmap[1].base = 0x100000;
    memmap[1].sz = memSize * 1024;
    memmap[1].type = NEXBOOT_MEM_FREE;
    memEntry = 2;
    return true;
}

// Reserve a mememory region
bool NbFwResvMem (uintptr_t base, size_t sz, int type)
{
    assert (memEntry < 256);
    // Attempt to find a conflicting region
    for (int i = 0; i < memEntry; ++i)
    {
        // Check for an overlap
        if (base == memmap[i].base)
        {
            if (memmap[i].type != NEXBOOT_MEM_FREE && memmap[i].type != type)
                return false;
            // Figure out how much space should be left in this region
            memmap[i].base = base + sz;
            if ((int64_t) (memmap[i].sz - sz) < 0)
            {
                memmap[i].base = 0;
                memmap[i].sz = 0;
            }
            else
                memmap[i].sz -= sz;
        }
        // Maybe the base of the new region is inside the base of the old?
        else if (base > memmap[i].base && base < (memmap[i].base + memmap[i].sz))
        {
            if (memmap[i].type != NEXBOOT_MEM_FREE && memmap[i].type != type)
                return false;
            // Check if we need to split this region
            if ((base + sz) < (memmap[i].base + memmap[i].sz))
            {
                // Create a new region
                memmap[memEntry].base = base + sz;
                memmap[memEntry].sz = (memmap[i].base + memmap[i].sz) - (base + sz);
                memmap[memEntry].flags = 0;
                memmap[memEntry].type = NEXBOOT_MEM_FREE;
                ++memEntry;
            }
            memmap[i].sz = base - memmap[i].base;
        }
        // Maybe the size of the new region protrudes into the new
        else if (memmap[i].base > base && (base + sz) < (memmap[i].base + memmap[i].sz) &&
                 (base + sz) > memmap[i].base)
        {
            if (memmap[i].type != NEXBOOT_MEM_FREE && memmap[i].type != type)
                return false;
            // Offset region's base and size
            memmap[i].sz = (memmap[i].base + memmap[i].sz) - (base + sz);
            memmap[i].base += ((base + sz) - memmap[i].base);
        }
        // Check if old region is consumed
        else if ((base + sz) >= (memmap[i].base + memmap[i].sz) && memmap[i].base > base)
        {
            if (memmap[i].type != NEXBOOT_MEM_FREE && memmap[i].type != type)
                return false;
            memmap[i].sz = 0;
            memmap[i].base = 0;
        }
    }
    // Create new memory entry for region
    memmap[memEntry].base = base;
    memmap[memEntry].sz = sz;
    memmap[memEntry].flags = 0;
    memmap[memEntry].type = type;
    ++memEntry;
    return true;
}

// Performs memory detection
void NbFwMemDetect()
{
    // Try using E820
    if (nbMemWithE820())
        goto bootResv;
    // Try AX=0xE881 or AX=E801
    if (nbMemWithE881())
        goto bootResv;
    // Try AH=8Ah or AX=DA88h
    if (nbMemWith8A())
        goto bootResv;
    // Try AH=88h
    if (nbMemWith88())
        goto bootResv;

    // That's an error
    NbLogMessageEarly ("nexboot: error: not supported memory map found",
                       NEXBOOT_LOGLEVEL_EMERGENCY);
    NbCrash();
bootResv:
    NbLogMessageEarly ("\r\n", NEXBOOT_LOGLEVEL_DEBUG);
}

NbMemEntry_t* NbGetMemMap (int* size)
{
    *size = memEntry;
    return memmap;
}

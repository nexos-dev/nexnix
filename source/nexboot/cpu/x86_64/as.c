/*
    as.c - contains address space management code
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
#include <nexboot/shell.h>
#include <string.h>

// This module is designed to be ultra simple. It makes a few assumptions,
// the big one being that all page tables come from identity mapped address space
// This makes the code fairly simple, as accessing the page tables is easy.
// Also, it is designed to be very temporary and rudimentary, as we have very basic
// requirements

typedef uint64_t pmle_t;    // We use one type for all PMLs

// Page table flags
#define PT_P                   (1ULL << 0)
#define PT_RW                  (1ULL << 1)
#define PT_WT                  (1ULL << 3)
#define PT_G                   (1ULL << 8)
#define PT_FRAME               0x7FFFFFFFFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Virtual address manipulating macros
// Shift table for each level
static uint8_t idxShiftTab[] = {0, 12, 21, 30, 39, 48};

// Macro to get level
#define AS_IDX_MASK               0x1FF
#define AS_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (AS_IDX_MASK))

// Canonicalizing functions

// According to the Intel manual, bits 63:48 in an address must equal bit 47
// However, in order to index page tables, bits 63:48 must be clear
// Note, however, with LA57, this changes things

#ifdef NEXNIX_X86_64_LA57
#define AS_TOP_ADDR_BIT   (1ULL << 56)
#define AS_CANONICAL_VAL  0xFE00000000000000
#define AS_CANONICAL_MASK 0x1FFFFFFFFFFFFFF
#else
#define AS_TOP_ADDR_BIT   (1ULL << 47)
#define AS_CANONICAL_VAL  0xFFFF000000000000
#define AS_CANONICAL_MASK 0x0000FFFFFFFFFFFF
#endif

static inline uintptr_t cpuAsMakeCanonical (uintptr_t addr)
{
    // Check if top bit is set
    if (addr & AS_TOP_ADDR_BIT)
        return addr |= AS_CANONICAL_VAL;    // Set top bits
    return addr;                            // Address already is canonical
}

static inline uintptr_t cpuAsDecanonical (uintptr_t addr)
{
    // Clear top bits
    return addr & AS_CANONICAL_MASK;
}

// Max level constant
#ifdef NEXNIX_X86_64_LA57
static int asMaxLevel = 5;
#else
static int asMaxLevel = 4;
#endif

// Top level paging structure
static pmle_t* pgBase = NULL;

void NbCpuAsInit()
{
    // Read in CR3
    pgBase = (pmle_t*) NbReadCr3();
    assert (pgBase);
#ifdef NEXNIX_X86_64_LA57
    // Check for LA57
    uint64_t cr4 = NbReadCr4();
    if (!(cr4 & NB_CR4_LA57))
    {
        // Error out
        NbLogMessage ("nexboot: LA57 not supported. Please use non-LA57 image\n",
                      NEXBOOT_LOGLEVEL_CRITICAL);
        NbCrash();
    }
#endif
    // Disable WP. We only do this because some not smart UEFIs
    // write protect paging structures
    uint64_t cr0 = NbReadCr0();
    NbWriteCr0 (cr0 & ~(NB_CR0_WP));
}

// Gets paging entry as current level
static inline pmle_t* cpuAsGetEntry (pmle_t* curTab, uintptr_t addr, int level)
{
    int idx = AS_IDX_LEVEL (addr, level);
    return &curTab[idx];
}

// Allocates paging structure and puts it in specified level
static pmle_t* cpuAsAllocSt (pmle_t* curSt, uintptr_t addr, int level, uint64_t flags)
{
    pmle_t* newSt = (pmle_t*) NbFwAllocPage();
    if (!newSt)
        return NULL;
    memset (newSt, 0, NEXBOOT_CPU_PAGE_SIZE);
    // Map it
    curSt[AS_IDX_LEVEL (addr, level)] = (paddr_t) newSt | flags;
    return newSt;
}

bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags)
{
    // Translate flags to CPU dependent flags
    uint64_t ptFlags = PT_P;
    if (flags & NB_CPU_AS_RW)
        ptFlags |= PT_RW;
    if (flags & NB_CPU_AS_WT)
        ptFlags |= PT_WT;
    //  De-canonicalize the address
    virt = cpuAsDecanonical (virt);
    // Loop through each level
    pmle_t* curSt = pgBase;
    for (int i = asMaxLevel; i > 1; --i)
    {
        // Grab entry
        pmle_t* ent = cpuAsGetEntry (curSt, virt, i);
        // Check if we need to map a table into it
        if (*ent)
        {
            // Check if flags need to be adjusted
            if (((ptFlags & PT_RW) && !(*ent & PT_RW)))
            {
                // Adjust them
                *ent &= PT_FRAME;
                *ent |= (ptFlags & ~(PT_G) & ~(PT_WT));
            }
            // Get structure
            curSt = (pmle_t*) PT_GETFRAME (*ent);
        }
        else
        {
            curSt = cpuAsAllocSt (curSt, virt, i, ptFlags);
            if (!curSt)
                return false;
        }
    }
    // Grab last PML entryX
    pmle_t* lastEnt = cpuAsGetEntry (curSt, virt, 1);
    // Map it
    *lastEnt = phys | ptFlags;
    // Invalidate TLB
    NbInvlpg (virt);
    return true;
}

void NbCpuAsUnmap (uintptr_t virt)
{
    // Decanonicalize
    virt = cpuAsDecanonical (virt);
    // Iterate through levels
    pmle_t* curSt = pgBase;
    for (int i = asMaxLevel; i > 1; --i)
    {
        // Get entry
        pmle_t* ent = cpuAsGetEntry (curSt, virt, i);
        if (!(*ent))
            return;                              // Address not actually mapped
        curSt = (pmle_t*) PT_GETFRAME (*ent);    // Get structure
    }
    // Grab last PML entry
    pmle_t* lastEnt = cpuAsGetEntry (curSt, virt, 1);
    *lastEnt = 0;    // Unmap
    NbInvlpg (virt);
}

// Enables paging
void NbCpuEnablePaging()
{
    // Paging is always enabled x86_64
}

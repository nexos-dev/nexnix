/*
    mul.c - contains MMU management layer
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

#include "mul.h"
#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>

// Global representing max page level
static int mulMaxLevel = 0;

// Canocicalizing helpers
static inline uintptr_t mulMakeCanonical (uintptr_t addr)
{
    // Check if top bit is set
    if (addr & MUL_TOP_ADDR_BIT)
        return addr |= MUL_CANONICAL_VAL;    // Set top bits
    return addr;                             // Address already is canonical
}

static inline uintptr_t mulDecanonical (uintptr_t addr)
{
    // Clear top bits
    return addr & MUL_CANONICAL_MASK;
}

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Set max page level if it hasn't been set
    if (!mulMaxLevel)
    {
#ifdef NEXNIX_X86_64_LA57
        mulMaxLevel = 5;
#else
        mulMaxLevel = 4;
#endif
    }
    uintptr_t pgAddr = mulDecanonical (virt);
    // Grab CR3
    pmle_t* curSt = (pmle_t*) CpuReadCr3();
    for (int i = mulMaxLevel; i > 1; --i)
    {
        // Get entry for this level
        pmle_t* ent = &curSt[MUL_IDX_LEVEL (pgAddr, i)];
        if (!(*ent))
            NkPanic ("cannot get physical address of non-existant page");
        // Get physical address
        curSt = (pmle_t*) PT_GETFRAME (*ent);
    }
    return PT_GETFRAME (curSt[MUL_IDX_LEVEL (pgAddr, 1)]);
}

// Maps a page early in the boot process
// This functions takes many shortcuts and makes many assumptions that are only
// valid during early boot
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
    // Set max page level if it hasn't been set
    if (!mulMaxLevel)
    {
#ifdef NEXNIX_X86_64_LA57
        mulMaxLevel = 5;
#else
        mulMaxLevel = 4;
#endif
    }
    // Decanonicalize address
    uintptr_t pgAddr = mulDecanonical (virt);
    // Translate flags
    uint64_t pgFlags = PF_P | PF_US;
    if (flags & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (flags & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (flags & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (flags & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    // Grab CR3
    pmle_t* curSt = (pmle_t*) CpuReadCr3();
    for (int i = mulMaxLevel; i > 1; --i)
    {
        // Get entry for this level
        pmle_t* ent = &curSt[MUL_IDX_LEVEL (pgAddr, i)];
        // Is it mapped?
        if (*ent)
        {
            // If PF_US is not set here and is set in pgFlags, panic
            // We try to keep kernel mappings well isolated from user mappings
            if (!(*ent & PF_US) && pgFlags & PF_US)
                NkPanic ("nexke: cannot map user page to kernel memory area");
            // Grab the structure and move to next level
            curSt = (pmle_t*) (PT_GETFRAME (*ent));
        }
        else
        {
            // Allocate a new table
            pmle_t* newSt = (pmle_t*) MmMulGetPhysEarly ((uintptr_t) MmBootPoolAlloc());
            // Determine new flags
            uint32_t tabFlags = PF_P | PF_RW;
            if (pgFlags & PF_US)
                tabFlags |= PF_US;
            // Map it
            curSt[MUL_IDX_LEVEL (pgAddr, i)] = tabFlags | (paddr_t) newSt;
            curSt = newSt;
        }
    }
    // Map the last entry
    pmle_t* lastEnt = &curSt[MUL_IDX_LEVEL (pgAddr, 1)];
    if (*lastEnt)
        NkPanic ("nexke: cannot map already mapped page");
    *lastEnt = pgFlags | phys;
    // Invalidate TLB
    CpuInvlpg (virt);
}

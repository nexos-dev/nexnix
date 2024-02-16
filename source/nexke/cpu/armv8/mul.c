/*
    mul.h - contains MUL header
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
static int mulMaxLevel = 4;

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Grab TTBR
    uintptr_t ttbr = 0;
    uintptr_t pgAddr = virt & MUL_CANONICAL_MASK;
    if (virt & MUL_CANONICAL_BIT)
        ttbr = CpuReadMsr ("TTBR1_EL1") & ~(1 << 0);
    else
        ttbr = CpuReadMsr ("TTBR0_EL1") & ~(1 << 0);
    pte_t* curSt = (pte_t*) ttbr;
    for (int i = mulMaxLevel; i > 1; --i)
    {
        // Get entry for this level
        pte_t* ent = &curSt[MUL_IDX_LEVEL (pgAddr, i)];
        if (!(*ent))
            NkPanic ("cannot get physical address of non-existant page");
        // Get physical address
        curSt = (pte_t*) PF_GETFRAME (*ent);
    }
    return PF_GETFRAME (curSt[MUL_IDX_LEVEL (pgAddr, 1)]);
}

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
    // Translate flags
    uint64_t pgFlags = PF_V | PF_RO | PF_AF | PF_PG | PF_EL0;
    if (flags & MUL_PAGE_RW)
        pgFlags &= ~(PF_RO);
    if (flags & MUL_PAGE_KE)
        pgFlags &= ~(PF_EL0);
    if (flags & MUL_PAGE_CD || flags & MUL_PAGE_WT)
        pgFlags |= (1 << 2);    // Use MAIR entry 1, which is device memory. This is very strict
                                //  and not at all optimal
    // Grab TTBR
    uintptr_t ttbr = 0;
    uintptr_t pgAddr = virt & MUL_CANONICAL_MASK;
    if (virt & MUL_CANONICAL_BIT)
        ttbr = CpuReadMsr ("TTBR1_EL1") & ~(1 << 0);
    else
        ttbr = CpuReadMsr ("TTBR0_EL1") & ~(1 << 0);
    pte_t* curSt = (pte_t*) ttbr;
    for (int i = mulMaxLevel; i > 1; --i)
    {
        // Get entry for this level
        pte_t* ent = &curSt[MUL_IDX_LEVEL (pgAddr, i)];
        // Is it mapped?
        if (*ent)
        {
            // If PF_EL0 is not set here and is set in pgFlags, panic
            // We try to keep kernel mappings well isolated from user mappings
            if (!(*ent & PF_EL0) && pgFlags & PF_EL0)
                NkPanic ("nexke: cannot map user page to kernel memory area");
            // Grab the structure and move to next level
            curSt = (pte_t*) (PF_GETFRAME (*ent));
        }
        else
        {
            // Allocate a new table
            pte_t* newSt = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage()->vaddr);
            // Determine new flags
            uint32_t tabFlags = PF_V | PF_PG;
            // Map it
            curSt[MUL_IDX_LEVEL (pgAddr, i)] = tabFlags | (paddr_t) newSt;
            curSt = newSt;
        }
    }
    // Map the last entry
    pte_t* lastEnt = &curSt[MUL_IDX_LEVEL (pgAddr, 1)];
    if (*lastEnt)
        NkPanic ("nexke: cannot map already mapped page");
    *lastEnt = pgFlags | phys;
    // Invalidate TLB
    asm volatile("dsb ishst; tlbi vae1, %0" : : "r"(virt >> 12));
}

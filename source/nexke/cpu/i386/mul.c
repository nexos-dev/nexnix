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
#include <string.h>

static pte_t* mulEarlyAllocTab (pde_t* pdir, uintptr_t virt, int flags)
{
    pte_t* tab = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage()->vaddr);
    memset (tab, 0, NEXKE_CPU_PAGESZ);
    // Grab PDE
    pde_t* tabPde = &pdir[PG_ADDR_DIR (virt)];
    if (flags & MUL_PAGE_KE)
        *tabPde = (paddr_t) tab | PF_P | PF_RW;
    else
        *tabPde = (paddr_t) tab | PF_P | PF_RW | PF_US;
    // Return it
    return tab;
}

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
    // Translate flags
    uint32_t pgFlags = PF_P | PF_US;
    if (flags & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (flags & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (flags & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (flags & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    // Get indices
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDE
    pde_t* dir = (pde_t*) CpuReadCr3();
    pde_t* pde = &dir[dirIdx];
    // Check if a table is mapped
    pte_t* pgTab = NULL;
    if (*pde)
    {
        // Get from PDE
        pgTab = (pte_t*) PT_GETFRAME (*pde);
    }
    else
    {
        // Allocate new page table
        pgTab = mulEarlyAllocTab (dir, virt, flags);
    }
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    if (*pte)
        NkPanic ("nexke: cannot map mapped page");
    *pte = phys | pgFlags;
    CpuInvlpg (virt);
}

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Get indices
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDE
    pde_t* dir = (pde_t*) CpuReadCr3();
    pde_t* pde = &dir[dirIdx];
    // Check if a table is mapped
    pte_t* pgTab = NULL;
    if (*pde)
    {
        // Get from PDE
        pgTab = (pte_t*) PT_GETFRAME (*pde);
    }
    else
    {
        NkPanic ("nexke: cannot get physical address of non-existant page");
    }
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    return PT_GETFRAME (*pte);
}

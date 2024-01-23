/*
    mulpae.c - contains MMU management layer for PAE systems
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

static pte_t* mulAllocTabEarly (pde_t* pdir, uintptr_t virt, int flags)
{
    pte_t* tab = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmBootPoolAlloc());
    // Grab PDE
    pde_t* tabPde = &pdir[PG_ADDR_DIR (virt)];
    if (flags & MUL_PAGE_KE)
        *tabPde = (paddr_t) tab | PF_P | PF_RW;
    else
        *tabPde = (paddr_t) tab | PF_P | PF_RW | PF_US;
    // Return it
    return tab;
}

static pde_t* mulAllocDirEarly (pdpte_t* pdpt, uintptr_t virt)
{
    pde_t* dir = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmBootPoolAlloc());
    // Map it
    pdpt[PG_ADDR_PDPT (virt)] = PF_P | (paddr_t) dir;
    // Reload PDPTE registers
    uint32_t dirp = CpuReadCr3();
    CpuWriteCr3 (dirp);
    return dir;
}

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
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
    // Get indices
    uint32_t pdptIdx = PG_ADDR_PDPT (virt);
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDPTE
    pdpte_t* pdpt = (pdpte_t*) CpuReadCr3();
    pdpte_t* pdpte = &pdpt[pdptIdx];
    pde_t* pdir = NULL;
    if (*pdpte)
    {
        // Get it
        pdir = (pde_t*) PT_GETFRAME (*pdpte);
    }
    else
    {
        // Allocate new directory
        pdir = mulAllocDirEarly (pdpt, virt);
    }
    pde_t* pde = &pdir[dirIdx];
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
        pgTab = mulAllocTabEarly (pdir, virt, flags);
    }
    pte_t* pte = &pgTab[tabIdx];
    if (*pte)
        NkPanic ("nexke: cannot map mapped address");
    *pte = pgFlags | phys;
    CpuInvlpg (virt);
}

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Get indices
    uint32_t pdptIdx = PG_ADDR_PDPT (virt);
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDPTE
    pdpte_t* pdpt = (pdpte_t*) CpuReadCr3();
    pdpte_t* pdpte = &pdpt[pdptIdx];
    pde_t* dir = NULL;
    // Check if dir is mapped
    if (*pdpte)
        dir = (pde_t*) PT_GETFRAME (*pdpte);
    else
        NkPanic ("nexke: cannot get physical address of non-existant page");
    // Grab PDE
    pde_t* pde = &dir[dirIdx];
    // Check if a table is mapped
    pte_t* pgTab = NULL;
    if (*pde)
    {
        // Get from PDE
        pgTab = (pte_t*) PT_GETFRAME (*pde);
    }
    else
        NkPanic ("nexke: cannot get physical address of non-existant page");
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    return PT_GETFRAME (*pte);
}

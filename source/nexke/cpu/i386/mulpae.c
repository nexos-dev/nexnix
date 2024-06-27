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

#include <nexke/cpu.h>
#include <nexke/cpu/ptab.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Kernel page directory template
static pte_t* mulKePgDir = NULL;

// Flushes whole TLB
void MmMulFlushTlb()
{
    CpuWriteCr3 (CpuReadCr3());
}

// Initializes MUL
void MmMulInit()
{
    MmPtabInit (2);    // Initialize page table manager with 2 levels
    // Grab page directory
    pte_t* pdpt = (pte_t*) CpuReadCr3();
    // On i386, we don't need to allocate a special page table for the page table cache
    // This is because the stack is mapped in that table already, so it has been created for us
    // But we do need to map the pages necessary for the cache to operate
    // So allocate cache entry page
    paddr_t cachePage = MmAllocPage()->pfn * NEXKE_CPU_PAGESZ;
    // Map it
    MmMulMapEarly (MUL_PTCACHE_ENTRY_BASE, cachePage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map the page table for the table cache
    pte_t* dir = (pte_t*) (pdpt[PG_ADDR_PDPT (MUL_PTCACHE_ENTRY_BASE)] & PT_FRAME);
    paddr_t cacheTab = dir[PG_ADDR_DIR (MUL_PTCACHE_TABLE_BASE)] & PT_FRAME;
    MmMulMapEarly (MUL_PTCACHE_TABLE_BASE, cacheTab, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Clear out all user PDEs
    pdpt[0] = 0;
    pdpt[1] = 0;
    // Write out CR3 to flush TLB
    CpuWriteCr3 ((uint32_t) pdpt);
    // Set base of directory
    MmGetKernelSpace()->mulSpace.base = (paddr_t) pdpt;
    // Prepare page table cache
    MmPtabInitCache (MmGetKernelSpace());
}

// Verifies mappability of pte2 into pte1
void MmMulVerify (pte_t pte1, pte_t pte2)
{
    // Make sure we aren't mapping user mapping into kernel region
    if (!(pte1 & PF_US) && pte2 & PF_US)
        NkPanic ("nexke: error: can't map user mapping into kernel memory");
}

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent)
{
    // Figure out if this is a kernel address
    bool isKernel = false;
    if (addr >= NEXKE_KERNEL_BASE)
        isKernel = true;
    else
        isKernel = false;
    // Allocate the table
    paddr_t tab = MmAllocPage()->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, tab, false);
    memset (cacheEnt->addr, 0, NEXKE_CPU_PAGESZ);
    MmPtabReturnCache (space, cacheEnt);
    // Set PTE
    pte_t flags = PF_P | PF_RW;
    if (!isKernel)
        flags |= PF_US;
    // Map it
    *ent = tab | flags;
    return tab;
}

// Allocates a page directory into ent
static paddr_t mulAllocDir (MmSpace_t* space, pdpte_t* ent)
{
    paddr_t tab = MmAllocPage()->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, tab, false);
    memset ((void*) cacheEnt->addr, 0, NEXKE_CPU_PAGESZ);
    MmPtabReturnCache (space, cacheEnt);
    *ent = PF_P | tab;
    // Flush PDPTE registers on CPU
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlushTlb();
    return tab;
}

// Creates an MUL address space
void MmMulCreateSpace (MmSpace_t* space)
{
}

// Destroys an MUL address space
void MmMulDestroySpace (MmSpace_t* space)
{
}

// Maps page into address space
void MmMulMapPage (MmSpace_t* space, uintptr_t virt, MmPage_t* page, int perm)
{
    // Translate flags
    pte_t pgFlags = PF_P | PF_US;
    // Check for NX
    if (CpuGetFeatures() & CPU_FEATURE_XD)
        pgFlags |= PF_NX;
    if (perm & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (perm & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (perm & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (perm & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    if (perm & MUL_PAGE_X)
        pgFlags &= ~(PF_NX);
    pte_t pte = pgFlags | (page->pfn * NEXKE_CPU_PAGESZ);
    // Check if we need a new page directory
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base, true);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    paddr_t pdir = 0;
    if (!(pdpt[PG_ADDR_PDPT (virt)] & PF_P))
    {
        // We need to allocate a page directory
        pdir = mulAllocDir (space, &pdpt[PG_ADDR_PDPT (virt)]);
    }
    else
    {
        pdir = pdpt[PG_ADDR_PDPT (virt)] & PT_FRAME;
    }
    MmPtabReturnCache (space, cacheEnt);
    MmPtabWalkAndMap (space, pdir, virt, pte);
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlush (virt);
}

// Unmaps page out of address space
void MmMulUnmapPage (MmSpace_t* space, uintptr_t virt)
{
    // Get page directory to unmap
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base, true);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    // Check if it is valid
    if (!(pdpt[PG_ADDR_PDPT (virt)] & PF_P))
        NkPanic ("nexke: cannot unmap invalid address");
    paddr_t pdirAddr = pdpt[PG_ADDR_PDPT (virt)] & PT_FRAME;
    MmPtabReturnCache (space, cacheEnt);
    MmPtabWalkAndUnmap (space, pdirAddr, virt);
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlush (virt);
}

// Gets mapping for specified virtual address
MmPage_t* MmMulGetMapping (MmSpace_t* space, uintptr_t virt)
{
    // Get page directory to unmap
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space, space->mulSpace.base, true);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    // Check if it is valid
    if (!(pdpt[PG_ADDR_PDPT (virt)] & PF_P))
        NkPanic ("nexke: cannot unmap invalid address");
    paddr_t pdirAddr = pdpt[PG_ADDR_PDPT (virt)] & PT_FRAME;
    MmPtabReturnCache (space, cacheEnt);
    // Grab address
    paddr_t addr = MmPtabGetPte (space, pdirAddr, virt) & PT_FRAME;
    return MmFindPagePfn (addr / NEXKE_CPU_PAGESZ);
}

static pte_t* mulAllocTabEarly (pde_t* pdir, uintptr_t virt, int flags)
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

static pde_t* mulAllocDirEarly (pdpte_t* pdpt, uintptr_t virt)
{
    pde_t* dir = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage()->vaddr);
    memset (dir, 0, NEXKE_CPU_PAGESZ);
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
        NkPanic ("nexke: error: cannot map mapped address");
    *pte = pgFlags | phys;
    MmMulFlush (virt);
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

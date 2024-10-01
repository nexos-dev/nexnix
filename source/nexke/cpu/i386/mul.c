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

#include <assert.h>
#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Is INVLPG support?
static bool isInvlpg = false;

// Kernel directory version
static int mulKeVersion = 0;

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
    pte_t* pd = (pte_t*) CpuReadCr3();
    // On i386, we don't need to allocate a special page table for the page table cache
    // This is because the stack is mapped in that table already, so it has been created for us
    // But we do need to map the pages necessary for the cache to operate
    // So allocate cache entry page
    paddr_t cachePage = MmAllocPage()->pfn * NEXKE_CPU_PAGESZ;
    // Map it
    MmMulMapEarly (MUL_PTCACHE_ENTRY_BASE, cachePage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map the page table for the table cache
    paddr_t cacheTab = pd[MUL_IDX_LEVEL (MUL_PTCACHE_TABLE_BASE, 2)] & PT_FRAME;
    MmMulMapEarly (MUL_PTCACHE_TABLE_BASE, cacheTab, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map kernel directory
    MmMulMapEarly ((uintptr_t) NEXKE_KERNEL_DIRBASE,
                   (paddr_t) pd,
                   MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Clear out all user PDEs
    memset (pd, 0, MUL_MAX_USER * sizeof (pte_t));
    // Write out CR3 to flush TLB
    CpuWriteCr3 ((uint32_t) pd);
    // Determine if invlpg is supported
    if (CpuGetFeatures() & CPU_FEATURE_INVLPG)
        isInvlpg = true;
    // Setup MUL
    memset (&MmGetKernelSpace()->mulSpace, 0, sizeof (MmMulSpace_t));
    MmGetKernelSpace()->mulSpace.base = (paddr_t) pd;
    MmGetKernelSpace()->mulSpace.keVersion = 0;
    MmAddPage (&MmGetKernelSpace()->mulSpace.tablePages,
               MmFindPagePfn (cachePage / NEXKE_CPU_PAGESZ),
               MUL_PTCACHE_TABLE_BASE - MmGetKernelSpace()->startAddr);
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
    {
        isKernel = true;
        assert (space == MmGetKernelSpace());
        ++mulKeVersion;
    }
    else
        isKernel = false;
    // Allocate the table
    MmPage_t* pg = MmAllocPage();
    paddr_t tab = pg->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmMulZeroPage (pg);
    // Add to page list
    MmAddPage (&space->mulSpace.tablePages, pg, addr - space->startAddr);
    // Set PTE
    pte_t flags = PF_P | PF_RW;
    if (!isKernel)
        flags |= PF_US;
    // Map it
    *ent = tab | flags;
    return tab;
}

void MmMulFlushCacheEntry (uintptr_t addr)
{
    if (isInvlpg)
        MmMulFlush (addr);
    else
        MmMulFlushTlb();
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
    if (perm & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (perm & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (perm & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (perm & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    pte_t pte = pgFlags | (page->pfn * NEXKE_CPU_PAGESZ);
    MmPtabWalkAndMap (space, space->mulSpace.base, virt, pte);
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
    {
        if (isInvlpg)
            MmMulFlush (virt);
        else
        {
            // If this is a user mapping, simply mark this space for flushing when we return to user
            // mode If a kernel mapping, we unfortunatly have to do it know
            if (space != MmGetKernelSpace())
                space->mulSpace.tlbUpdatePending = true;
            else
                MmMulFlushTlb();
        }
    }
    // Add mapping to this page
    MmPageAddMap (page, space, virt);
}

// Unmaps page out of address space
void MmMulUnmapPage (MmSpace_t* space, uintptr_t virt)
{
    MmPtabWalkAndUnmap (space, space->mulSpace.base, virt);
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
    {
        if (isInvlpg)
            MmMulFlush (virt);
        else
        {
            // If this is a user mapping, simply mark this space for flushing when we return to user
            // mode If a kernel mapping, we unfortunatly have to do it know
            if (space != MmGetKernelSpace())
                space->mulSpace.tlbUpdatePending = true;
            else
                MmMulFlushTlb();
        }
    }
}

// Changes protection for a mapping
void MmMulChangePerm (MmSpace_t* space, uintptr_t virt, int perm)
{
    // Translate flags
    pte_t pgFlags = PF_P | PF_US;
    if (perm & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (perm & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (perm & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (perm & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    MmPtabWalkAndChange (space, space->mulSpace.base, virt, pgFlags);
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
    {
        if (isInvlpg)
            MmMulFlush (virt);
        else
        {
            // If this is a user mapping, simply mark this space for flushing when we return to user
            // mode If a kernel mapping, we unfortunatly have to do it know
            if (space != MmGetKernelSpace())
                space->mulSpace.tlbUpdatePending = true;
            else
                MmMulFlushTlb();
        }
    }
}

// Gets mapping for specified virtual address
MmPage_t* MmMulGetMapping (MmSpace_t* space, uintptr_t virt)
{
    // Grab address
    paddr_t addr = MmPtabGetPte (space, space->mulSpace.base, virt) & PT_FRAME;
    return MmFindPagePfn (addr / NEXKE_CPU_PAGESZ);
}

// MUL early routines

static pte_t* mulEarlyAllocTab (pde_t* pdir, uintptr_t virt, int flags)
{
    pte_t* tab = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage());
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
    // Check for INVLPG
    if (CpuGetFeatures() & CPU_FEATURE_INVLPG)
        MmMulFlush (virt);
    else
        MmMulFlushTlb();
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

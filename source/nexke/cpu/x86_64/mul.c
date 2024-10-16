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

// Initializes MUL
void MmMulInit()
{
    NkLogDebug ("nexke: intializing MUL\n");
#ifdef NEXNIX_X86_64_LA57
    int levels = 5;
#else
    int levels = 4;
#endif
    MmPtabInit (levels);    // Initialize page table manager with 4 levels
    // Grab top PML directory
    pte_t* pmlTop = (pte_t*) CpuReadCr3();
    // Allocate cache
    MmPage_t* cachePgCtrl = MmAllocPage();
    paddr_t cachePage = cachePgCtrl->pfn * NEXKE_CPU_PAGESZ;
    // Map it
    MmMulMapEarly (MUL_PTCACHE_ENTRY_BASE, cachePage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map dummy page at base so we have the structure created
    MmMulMapEarly (MUL_PTCACHE_BASE, 0, MUL_PAGE_R | MUL_PAGE_KE | MUL_PAGE_RW);
    // Find table for table cache
    paddr_t cacheTab = 0;
    pte_t* curSt = pmlTop;
    for (int i = levels; i > 2; --i)
    {
        int idx = MUL_IDX_LEVEL (mulDecanonical (MUL_PTCACHE_BASE), i);
        curSt = (pte_t*) (curSt[MUL_IDX_LEVEL (mulDecanonical (MUL_PTCACHE_BASE), i)] & PT_FRAME);
        assert (curSt);
    }
    cacheTab = curSt[MUL_IDX_LEVEL (mulDecanonical (MUL_PTCACHE_BASE), 2)] & PT_FRAME;
#define MUL_PTCACHE_PMLTOP_STAGE 0xFFFFFFFF7FFDC000
    MmMulMapEarly (MUL_PTCACHE_PMLTOP_STAGE,
                   (paddr_t) pmlTop,
                   MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    MmMulMapEarly (MUL_PTCACHE_TABLE_BASE, cacheTab, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    memset ((void*) MUL_PTCACHE_PMLTOP_STAGE, 0, (MUL_MAX_USER_PMLTOP * 8));
    // Write out CR3 to flush TLB
    CpuWriteCr3 ((uint64_t) pmlTop);
    // Setup MUL
    memset (&MmGetKernelSpace()->mulSpace, 0, sizeof (MmMulSpace_t));
    MmGetKernelSpace()->mulSpace.base = (paddr_t) pmlTop;
    NkListAddFront (&MmGetKernelSpace()->mulSpace.pageList, &cachePgCtrl->link);
    // Prepare page table cache
    MmPtabInitCache (MmGetKernelSpace());
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
    MmPage_t* pg = MmAllocPage();
    paddr_t tab = pg->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmMulZeroPage (pg);
    // Add to page list
    NkListAddFront (&space->mulSpace.pageList, &pg->link);
    // Set PTE
    pte_t flags = PF_P | PF_RW;
    if (!isKernel)
        flags |= PF_US;
    // Map it
    *ent = tab | flags;
    return tab;
}

// Verifies mappability of pte2 into pte1
void MmMulVerify (pte_t pte1, pte_t pte2)
{
    // Make sure we aren't mapping user mapping into kernel region
    if (!(pte1 & PF_US) && pte2 & PF_US)
        NkPanic ("nexke: error: can't map user mapping into kernel memory");
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
    MM_MUL_LOCK (space);
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
    MmPtabWalkAndMap (space, space->mulSpace.base, mulDecanonical (virt), pte);
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlush (virt);
    MM_MUL_UNLOCK (space);
    // Add mapping to this page
    MmPageAddMap (page, space, virt);
}

// Unmaps page out of address space
void MmMulUnmapPage (MmSpace_t* space, uintptr_t virt)
{
    MM_MUL_LOCK (space);
    MmPtabWalkAndUnmap (space, space->mulSpace.base, mulDecanonical (virt));
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlush (virt);
    MM_MUL_UNLOCK (space);
}

// Gets mapping for specified virtual address
MmPage_t* MmMulGetMapping (MmSpace_t* space, uintptr_t virt)
{
    MM_MUL_LOCK (space);
    // Grab address
    paddr_t addr = MmPtabGetPte (space, space->mulSpace.base, mulDecanonical (virt)) & PT_FRAME;
    MM_MUL_UNLOCK (space);
    return MmFindPagePfn (addr / NEXKE_CPU_PAGESZ);
}

// Early MUL functions

// Global representing max page level
static int mulMaxLevel = 0;

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
    pte_t* curSt = (pte_t*) CpuReadCr3();
    for (int i = mulMaxLevel; i > 1; --i)
    {
        // Get entry for this level
        pte_t* ent = &curSt[MUL_IDX_LEVEL (pgAddr, i)];
        if (!(*ent))
            NkPanic ("cannot get physical address of non-existant page");
        // Get physical address
        curSt = (pte_t*) PT_GETFRAME (*ent);
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
    pte_t* curSt = (pte_t*) CpuReadCr3();
    for (int i = mulMaxLevel; i > 1; --i)
    {
        // Get entry for this level
        pte_t* ent = &curSt[MUL_IDX_LEVEL (pgAddr, i)];
        // Is it mapped?
        if (*ent)
        {
            // Grab the structure and move to next level
            curSt = (pte_t*) (PT_GETFRAME (*ent));
        }
        else
        {
            // Allocate a new table
            pte_t* newSt = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage());
            memset (newSt, 0, NEXKE_CPU_PAGESZ);
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
    pte_t* lastEnt = &curSt[MUL_IDX_LEVEL (pgAddr, 1)];
    if (*lastEnt)
        NkPanic ("nexke: cannot map already mapped page");
    *lastEnt = pgFlags | phys;
    // Invalidate TLB
    CpuInvlpg (virt);
}

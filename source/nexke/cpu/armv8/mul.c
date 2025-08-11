/*
    mul.c - contains MUL implementation
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
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Global representing max page level
static int mulMaxLevel = 4;

// Page mapping cache
static SlabCache_t* mulMapCache = NULL;

// Basic helpers
static inline uintptr_t mulMakeCanonical (uintptr_t addr)
{
    // Check if top bit is set
    if (addr & MUL_TOP_ADDR_BIT)
        return addr |= MUL_CANONICAL_VAL;    // Set top bits
    return addr;                             // Address already is canonical
}

static inline uintptr_t mulDecanonical (uintptr_t addr)
{
    return addr & MUL_CANONICAL_MASK;
}

static inline uintptr_t mulReadTtbr1()
{
    return CpuReadSpr ("TTBR1_EL1") & 0xFFFFFFFFFFFFFFF0;
}

static inline uintptr_t mulReadTtbr0()
{
    return CpuReadSpr ("TTBR0_EL1") & 0xFFFFFFFFFFFFFFF0;
}

// Initializes MUL
void MmMulInit()
{
    NkLogDebug ("nexke: intializing MUL\n");
    MmPtabInit (mulMaxLevel);    // Initialize page table manager with 4 levels
    // Grab TTBR1 and TTBR0
    pte_t* ttbr1 = (pte_t*) mulReadTtbr1();
    pte_t* ttbr0 = (pte_t*) mulReadTtbr0();
    // Allocate cache
    MmPage_t* cachePgCtrl = MmAllocFixedPage();
    MmFixPage (cachePgCtrl);
    paddr_t cachePage = cachePgCtrl->pfn * NEXKE_CPU_PAGESZ;
    // Map it
    MmMulMapEarly (MUL_PTCACHE_ENTRY_BASE, cachePage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map dummy page at base so we have the structure created
    MmMulMapEarly (MUL_PTCACHE_BASE, 0, MUL_PAGE_R | MUL_PAGE_KE | MUL_PAGE_RW);
    // Find table for table cache
    paddr_t cacheTab = 0;
    pte_t* curSt = ttbr0;
    for (int i = mulMaxLevel; i > 2; --i)
    {
        int idx = MUL_IDX_LEVEL (mulDecanonical (MUL_PTCACHE_BASE), i);
        curSt = (pte_t*) (curSt[MUL_IDX_LEVEL (mulDecanonical (MUL_PTCACHE_BASE), i)] & PT_FRAME);
        assert (curSt);
    }
    cacheTab = curSt[MUL_IDX_LEVEL (mulDecanonical (MUL_PTCACHE_BASE), 2)] & PT_FRAME;
#define MUL_PTCACHE_TTBR0_STAGE 0xFFFFFFFF7FFDC000
    MmMulMapEarly (MUL_PTCACHE_TTBR0_STAGE,
                   (paddr_t) ttbr0,
                   MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    MmMulMapEarly (MUL_PTCACHE_TABLE_BASE, cacheTab, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    memset ((void*) MUL_PTCACHE_TTBR0_STAGE, 0, 511 * 8);
    // Flush out TLB
    MmMulFlushTlb();
    // Setup MUL
    MmMulSpace_t* mulSpace = &MmGetKernelSpace()->mulSpace;
    memset (mulSpace, 0, sizeof (MmMulSpace_t));
    mulSpace->base = (paddr_t) ttbr0;
    mulSpace->refCount = 1;
    NkListInit (&mulSpace->pageList);
    NkListAddFront (&mulSpace->pageList, &cachePgCtrl->link);
    // Prepare page table cache
    MmPtabInitCache (MmGetKernelSpace());
    mulMapCache = MmCacheCreate (sizeof (MmPageMap_t), "MmPageMap_t", 0, 0);
    assert (mulMapCache);
    // Set up MAIR
    uint64_t mair = (MUL_MAIR_NORMAL << MUL_MAIR0) | (MUL_MAIR_DEVICE << MUL_MAIR1) |
                    (MUL_MAIR_WT << MUL_MAIR2) | (MUL_MAIR_NON_CACHE << MUL_MAIR3) |
                    (MUL_MAIR_DEVICE_WC << MUL_MAIR4);
    CpuWriteSpr ("MAIR_EL1", mair);
    // Set up TCR
    uint64_t tcr = CpuReadSpr ("TCR_EL1");
    // Set IPS
    int paBits = CpuGetCcb()->archCcb.paBits;
    int i = 0;
    for (; i < MUL_TCR_MAX_IPS; ++i)
    {
        if (mulIpsToBits[i] == paBits)
            break;
    }
    tcr |= ((uint64_t) i << MUL_TCR_IPS);
    // Set up ASID things
    tcr &= ~(MUL_TCR_A1);
    if (CpuGetCcb()->archCcb.asidBits == 16)
        tcr |= MUL_TCR_ASID16;
    // Make sure T0SZ and T1SZ are set appropriatly
    int t0Sz = (tcr >> MUL_TCR_T0SZ) & MUL_TCR_TSZ_MASK;
    int t1Sz = (tcr >> MUL_TCR_T1SZ) & MUL_TCR_TSZ_MASK;
    if (t0Sz != 16 || t1Sz != 16)
        NkPanic ("nexke: invalid MMU setup detected");
    // Make sure TG0 and TG1 are good
    int tg0 = (tcr >> MUL_TCR_TG0) & MUL_TCR_FIELD;
    int tg1 = (tcr >> MUL_TCR_TG1) & MUL_TCR_FIELD;
    if (tg0 != 0 || tg1 != 2)
        NkPanic ("nexke: invalid MMU setup detected");
    CpuWriteSpr ("TCR_EL1", tcr);
}

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent)
{
    // Unlock address space quickly
    MM_MUL_UNLOCK (space);
    // Allocate the table
    MmPage_t* pg = MmAllocFixedPage();
    MmFixPage (pg);
    paddr_t tab = pg->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmMulZeroPage (pg);
    // Re-lock
    MM_MUL_LOCK (space);
    // Make sure a table wasn't already map while we were unlock
    if (*ent)
        tab = *ent & PT_FRAME;
    else
    {
        // Add to page list
        NkListAddFront (&space->mulSpace.pageList, &pg->link);
        // Set PTE
        pte_t flags = PF_V | PF_TAB;
        // Map it
        *ent = tab | flags;
    }
    return tab;
}

// Verifies mappability of pte2 into pte1
// No-op on armv8
void MmMulVerify (pte_t pte1, pte_t pte2)
{
}

// Creates an MUL address space
void MmMulCreateSpace (MmSpace_t* space)
{
}

// References an address space
void MmMulRefSpace (MmSpace_t* space)
{
    MM_MUL_LOCK (space);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    ++mulSpace->refCount;
    MM_MUL_UNLOCK (space);
}

// Destroys an MUL address space
void MmMulDeRefSpace (MmSpace_t* space)
{
    if (space == MmGetKernelSpace())
        NkPanic ("nexke: can't destroy kernel space");
    MM_MUL_LOCK (space);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    if (--mulSpace->refCount == 0)
    {
    }
    else
        MM_MUL_UNLOCK (space);
}

// Decides between kernel / user address space
static inline uintptr_t mmMulGetTtbr (MmMulSpace_t* space, uintptr_t virt)
{
    if (virt & MUL_CANONICAL_BIT)
        return mulReadTtbr1();    // Use TTBR1 for kernel addresses
    return space->base;
}

// Invalidates TLB
static inline void MmMulFlushAddr (MmSpace_t* space, uintptr_t addr)
{
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlush (addr);
}

// Translates permissions to table flags
static inline pte_t mmMulGetProt (int flags)
{
    // Translate flags
    uint64_t pgFlags = PF_V | PF_RO | PF_PG | PF_EL0 | PF_PXN | PF_UXN | PF_OSH | PF_MAIR_NORMAL;
    if (flags & MUL_PAGE_RW)
        pgFlags &= ~(PF_RO);
    if (flags & MUL_PAGE_X)
        pgFlags &= ~(PF_UXN | PF_PXN);
    if (flags & MUL_PAGE_KE)
    {
        pgFlags &= ~(PF_EL0);
        pgFlags |= PF_UXN;
    }
    if (flags & MUL_PAGE_DEV)
        pgFlags |= PF_MAIR_DEV;
    else if (flags & MUL_PAGE_CD)
        pgFlags |= PF_MAIR_UC;
    else if (flags & MUL_PAGE_WT)
        pgFlags |= PF_MAIR_WT;
    else if (flags & MUL_PAGE_WC)
        pgFlags |= PF_MAIR_WC;
    return pgFlags;
}

// Maps page into address space
void MmMulMapPage (MmSpace_t* space, uintptr_t virt, MmPage_t* page, int perm)
{
    MM_MUL_LOCK (space);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    // Translate flags
    pte_t pgFlags = mmMulGetProt (perm);
    // Set fixed flag if needed
    if (page->flags & MM_PAGE_FIXED)
        pgFlags |= PF_F;
    // Create PTE
    pte_t newPte = pgFlags | (page->pfn * NEXKE_CPU_PAGESZ);
    // Grab page table of last entry
    uint64_t ttbr = mmMulGetTtbr (mulSpace, virt);
    uint64_t canonVirt = virt;
    virt = mulDecanonical (virt);
    MmPtCacheEnt_t* cacheEnt = MmPtabWalkAndMap (space, ttbr, virt, newPte);
    // Get table and PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (virt, 1)];
    MmPage_t* oldPage = NULL;    // Set to the page we need to remove a mapping from
                                 // if the current PTE has a mapping in it
    // Check if the PTE already contains a translation
    if (*pte)
    {
        // Make sure current mapping isn't fixed
        if (*pte & PF_F)
            NkPanic ("nexke: attempt to unmap fixed mapping");
        // Check if the fixed state is changing
        if ((*pte & PF_F) != (newPte & PF_F))
        {
            // Keep stats accurate
            if (newPte & PF_F)
                ++space->stats.numFixed;
            else if (*pte & PF_F)
                --space->stats.numFixed;
        }
        // Check if we need to remove a mapping
        if ((*pte & PT_FRAME) != (newPte & PT_FRAME))
        {
            // We need to remove the old mapping
            // We can't do this until the address space is unlocked however
            // so just keep note of that
            oldPage = MmFindPagePfn ((*pte & PT_FRAME) >> NEXKE_CPU_PAGE_SHIFT);
            assert (oldPage);
        }
        // Set the PTE
        *pte = newPte;
        // Flush it
        MmMulFlushAddr (space, mulMakeCanonical (virt));
    }
    else
    {
        // Check if this is fixed
        if (newPte & PF_F)
            ++space->stats.numFixed;
        // Set it
        *pte = newPte;
    }
    // Return it
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
    // Check if we have a mapping to remove
    if (oldPage)
    {
        // Find this mapping
        NkSpinLock (&oldPage->lock);
        MmPageMap_t* map = oldPage->maps;
        MmPageMap_t* prev = NULL;
        while (map)
        {
            if (map->addr == virt && map->space == space)
            {
                // Remove it and break
                --space->stats.numMaps;
                if (prev)
                    prev->next = map->next;
                else
                    oldPage->maps = map->next;
                break;
            }
            prev = map;
            map = map->next;
        }
        NkSpinUnlock (&oldPage->lock);
    }
    if (!(page->flags & MM_PAGE_FIXED) && !(page->flags & MM_PAGE_UNUSABLE))
    {
        // Add this mapping
        MmPageMap_t* map = (MmPageMap_t*) MmCacheAlloc (mulMapCache);
        if (!map)
            NkPanicOom();
        map->addr = canonVirt;
        map->space = space;
        map->next = NULL;
        // Link it
        NkSpinLock (&page->lock);
        map->next = page->maps;
        page->maps = map;
        NkSpinUnlock (&page->lock);
    }
    // Update stats
    ++space->stats.numMaps;
}

// Unmaps a range out of an address space
void MmMulUnmapRange (MmSpace_t* space, uintptr_t base, size_t count)
{
    MM_MUL_LOCK (space);
    // Set up iterator
    MmPtIter_t iter = {0};
    iter.asPhys = mmMulGetTtbr (&space->mulSpace, base);
    iter.addr = mulDecanonical (base);
    iter.space = space;
    for (int i = 0; i < count; ++i)
    {
        uintptr_t addr = iter.addr;
        // Get cache entry for table containing PTE
        MmPtCacheEnt_t* cacheEnt = MmPtabIterate (&iter);
        // If there is no cache entry, move to next address
        if (cacheEnt)
        {
            // Get page table
            pte_t* table = (pte_t*) cacheEnt->addr;
            pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
            if (*pte)
            {
                // Make sure PTE isn't fixed
                if (*pte & PF_F)
                    NkPanic ("nexke: can't remove fixed mapping");
                // Get page of PTE
                MmPage_t* page = MmFindPagePfn ((*pte & PT_FRAME) >> NEXKE_CPU_PAGE_SHIFT);
                *pte = 0;
                MmMulFlushAddr (space, mulMakeCanonical (addr));
                // Now remove mapping, first we need to unlock the address space
                // as we can't lock the page while holding the address space lock
                // as that would violate lock ordering
                // I don't think this will cause any problems, but I could be wrong
                MM_MUL_UNLOCK (space);
                NkSpinLock (&page->lock);
                // Find the mapping
                MmPageMap_t* map = page->maps;
                MmPageMap_t* prev = NULL;
                while (map)
                {
                    if (map->addr == mulMakeCanonical (addr) && map->space == space)
                    {
                        // Remove it and break
                        --space->stats.numMaps;
                        if (prev)
                            prev->next = map->next;
                        else
                            page->maps = map->next;
                        break;
                    }
                    prev = map;
                    map = map->next;
                }
                NkSpinUnlock (&page->lock);
                MM_MUL_LOCK (space);
            }
        }
    }
    MmPtabEndIterate (&iter);
    MM_MUL_UNLOCK (space);
}

// Changes protection on range of address space
void MmMulProtectRange (MmSpace_t* space, uintptr_t base, size_t count, int perm)
{
    MM_MUL_LOCK (space);
    // Get right flags
    pte_t flags = mmMulGetProt (perm);
    // Set up iterator
    MmPtIter_t iter = {0};
    iter.asPhys = mmMulGetTtbr (&space->mulSpace, base);
    iter.addr = mulDecanonical (base);
    iter.space = space;
    for (int i = 0; i < count; ++i)
    {
        uintptr_t addr = iter.addr;
        // Get cache entry for table containing PTE
        MmPtCacheEnt_t* cacheEnt = MmPtabIterate (&iter);
        // If there is no cache entry, move to next address
        if (cacheEnt)
        {
            // Get page table
            pte_t* table = (pte_t*) cacheEnt->addr;
            pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
            // Set protection if PTE is valid
            if (*pte & PF_V)
            {
                *pte = (*pte & PT_FRAME) | flags | (*pte & PF_F);
                MmMulFlushAddr (space, mulMakeCanonical (addr));
            }
        }
    }
    MmPtabEndIterate (&iter);
    MM_MUL_UNLOCK (space);
}

// Unmaps a page and removes all its mappings
void MmMulUnmapPage (MmPage_t* page)
{
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        uint64_t addr = mulDecanonical (map->addr);
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt =
            MmPtabWalk (map->space, mmMulGetTtbr (mulSpace, map->addr), addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
        // Make sure it isn't fixed
        if (*pte & PF_F)
            NkPanic ("nexke: can't unmap fixed mapping");
        // Clear it
        *pte = 0;
        --map->space->stats.numMaps;
        // Flush TLB if needed
        MmMulFlushAddr (map->space, map->addr);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
    page->maps = NULL;
}

// Changes protection on a page
void MmMulProtectPage (MmPage_t* page, int perm)
{
    // Get right flags
    pte_t flags = mmMulGetProt (perm);
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        uint64_t addr = mulDecanonical (map->addr);
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt =
            MmPtabWalk (map->space, mmMulGetTtbr (mulSpace, map->addr), addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
        // Set flags
        *pte = (*pte & PT_FRAME) | flags | (*pte & PF_F);
        // Flush TLB if needed
        MmMulFlushAddr (map->space, map->addr);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
}

// Fixes a page in an address space
void MmMulFixPage (MmPage_t* page)
{
    assert (page->fixCount);
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        uint64_t addr = mulDecanonical (map->addr);
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt =
            MmPtabWalk (map->space, mmMulGetTtbr (mulSpace, map->addr), addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
        if (*pte == 0)
            goto next;
        // Check if this is actually changing the attribute
        if ((*pte & PF_F) == 0)
            ++map->space->stats.numFixed;
        // Set the attribute
        *pte |= PF_F;
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
}

// Unfixes a page
void MmMulUnfixPage (MmPage_t* page)
{
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        uint64_t addr = mulDecanonical (map->addr);
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt =
            MmPtabWalk (map->space, mmMulGetTtbr (mulSpace, map->addr), addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
        if (*pte == 0)
            goto next;
        // Check if this is actually changing the attribute
        if (*pte & PF_F)
            --map->space->stats.numFixed;
        // Set the attribute
        *pte &= ~(PF_F);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
}

// Gets mapping for specified virtual address
MmPage_t* MmMulGetMapping (MmSpace_t* space, uintptr_t virt)
{
    MM_MUL_LOCK (space);
    MmPtCacheEnt_t* cacheEnt =
        MmPtabWalk (space, mmMulGetTtbr (&space->mulSpace, virt), mulDecanonical (virt));
    assert (cacheEnt);
    // Get PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (mulDecanonical (virt), 1)];
    paddr_t addr = *pte & PT_FRAME;
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
    return MmFindPagePfn (addr / NEXKE_CPU_PAGESZ);
}

// Sets attribute of address
void MmMulSetAttr (MmSpace_t* space, uintptr_t virt, int attr, bool val)
{
    MM_MUL_LOCK (space);
    MmPtCacheEnt_t* cacheEnt =
        MmPtabWalk (space, mmMulGetTtbr (&space->mulSpace, virt), mulDecanonical (virt));
    assert (cacheEnt);
    // Get PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (mulDecanonical (virt), 1)];
    if (attr == MUL_ATTR_ACCESS)
        *pte |= PF_AF;
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
}

// Gets attribute of address
bool MmMulGetAttr (MmSpace_t* space, uintptr_t virt, int attr)
{
    MM_MUL_LOCK (space);
    MmPtCacheEnt_t* cacheEnt =
        MmPtabWalk (space, mmMulGetTtbr (&space->mulSpace, virt), mulDecanonical (virt));
    assert (cacheEnt);
    // Get PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (mulDecanonical (virt), 1)];
    bool attrVal = false;
    if (attr == MUL_ATTR_ACCESS)
        attrVal = !!(*pte & PF_AF);
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
    return attrVal;
}

// Early MUL

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Grab TTBR
    uintptr_t ttbr = 0;
    uintptr_t pgAddr = virt & MUL_CANONICAL_MASK;
    if (virt & MUL_CANONICAL_BIT)
        ttbr = CpuReadSpr ("TTBR1_EL1") & ~(1 << 0);
    else
        ttbr = CpuReadSpr ("TTBR0_EL1") & ~(1 << 0);
    pte_t* curSt = (pte_t*) ttbr;
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

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
    // Translate flags
    uint64_t pgFlags = PF_V | PF_RO | PF_AF | PF_PG | PF_EL0 | PF_PXN | PF_UXN | PF_OSH;
    if (flags & MUL_PAGE_RW)
        pgFlags &= ~(PF_RO);
    if (flags & MUL_PAGE_CD || flags & MUL_PAGE_WT || flags & MUL_PAGE_DEV)
        pgFlags |= (1 << 2);    // Use MAIR entry 1, which is device memory. This is very strict
                                //  and not at all optimal
    if (flags & MUL_PAGE_X)
        pgFlags &= ~(PF_UXN | PF_PXN);
    if (flags & MUL_PAGE_KE)
    {
        pgFlags &= ~(PF_EL0);
        pgFlags |= PF_UXN;
    }
    // Grab TTBR
    uintptr_t ttbr = 0;
    uintptr_t pgAddr = virt & MUL_CANONICAL_MASK;
    if (virt & MUL_CANONICAL_BIT)
        ttbr = CpuReadSpr ("TTBR1_EL1") & ~(1 << 0);
    else
        ttbr = CpuReadSpr ("TTBR0_EL1") & ~(1 << 0);
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
            curSt = (pte_t*) (PT_GETFRAME (*ent));
        }
        else
        {
            // Allocate a new table
            pte_t* newSt = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage());
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
    MmMulFlush (virt);
}

/*
    as.c - contains address space management code
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
#include <string.h>

// This module is designed to be ultra simple. It makes a few assumptions,
// the big one being that all page tables come from identity mapped address space
// This makes the code fairly simple, as accessing the page tables is easy.
// Also, it is designed to be very temporary and rudimentary, as we have very basic
// requirements

typedef uint32_t pde_t;
typedef uint32_t pte_t;

#define PT_P                   (1 << 0)
#define PT_RW                  (1 << 1)
#define PT_G                   (1 << 8)
#define PT_FRAME               0xFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Virtual address management macros
#define PG_ADDR_DIRSHIFT  22
#define PG_ADDR_TABSHIFT  12
#define PG_ADDR_TABMASK   0x3FF000
#define PG_ADDR_DIR(addr) ((addr) >> PG_ADDR_DIRSHIFT)
#define PG_ADDR_TAB(addr) (((addr) & (PG_ADDR_TABMASK)) >> PG_ADDR_TABSHIFT)

// The address space
static pde_t* pdir = NULL;
// Is paging on?
static bool isPgOn = true;

void NbCpuAsInit()
{
    // Grab dir from CR3
    pdir = (pde_t*) NbReadCr3();
    if (!pdir)
    {
        pdir = (pde_t*) NbFwAllocPage();
        isPgOn = false;
    }
    // Disable WP. We only do this because some not smart UEFIs
    // write protect paging structures
    uint64_t cr0 = NbReadCr0();
    NbWriteCr0 (cr0 & ~(NB_CR0_WP));
}

static pte_t* cpuAsAllocTab (uintptr_t virt, uint32_t flags)
{
    pte_t* tab = (pte_t*) NbFwAllocPage();
    if (!tab)
        return NULL;
    memset (tab, 0, NEXBOOT_CPU_PAGE_SIZE);
    // Unset PT_G
    flags &= ~(PT_G);
    // Grab PDE
    pde_t* tabPde = &pdir[PG_ADDR_DIR (virt)];
    *tabPde = 0;
    *tabPde |= flags;
    PT_SETFRAME (*tabPde, (paddr_t) tab);
    // Return it
    return tab;
}

bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags)
{
    // Translate flags to CPU dependent flags
    uint32_t ptFlags = PT_P;
    if (flags & NB_CPU_AS_RW)
        ptFlags |= PT_RW;
    // Grab PDE
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
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
        pgTab = cpuAsAllocTab (virt, ptFlags);
        if (!pgTab)
            return false;
    }
    // Set protection of PDE if nedded
    if (((ptFlags & PT_RW) && !(*pde & PT_RW)))
    {
        *pde &= PT_FRAME;
        *pde |= ptFlags;
    }
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    // Map it
    *pte = ptFlags;
    PT_SETFRAME (*pte, phys);
    // Invalidate TLB entry
    if (isPgOn)
        NbInvlpg (virt);
    return true;
}

void NbCpuAsUnmap (uintptr_t virt)
{
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    pde_t* pde = &pdir[dirIdx];
    // Check if a table is mapped
    if (!(*pde))
        return;
    pte_t* pgTab = (pte_t*) PT_GETFRAME (*pde);
    pte_t* pte = &pgTab[tabIdx];
    *pte = 0;
    if (isPgOn)
        NbInvlpg (virt);
}

// Enables paging
void NbCpuEnablePaging()
{
    NbWriteCr3 ((uint32_t) pdir);
    uint32_t cr0 = NbReadCr0();
    cr0 |= NB_CR0_PG;
    NbWriteCr0 (cr0);
    isPgOn = true;
}

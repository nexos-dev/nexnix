/*
    aspae.c - contains address space management code for PAE
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

typedef uint64_t pde_t;
typedef uint64_t pte_t;
typedef uint64_t pdpte_t;

#define PT_P                   (1ULL << 0)
#define PT_RW                  (1ULL << 1)
#define PT_G                   (1 << 8)
#define PT_NX                  0x8000000000000000
#define PT_FRAME               0x7FFFFFFFFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Virtual address management macros
#define PG_ADDR_PDPTSHIFT  30
#define PG_ADDR_DIRSHIFT   21
#define PG_ADDR_DIRMASK    0x3FE00000
#define PG_ADDR_TABSHIFT   12
#define PG_ADDR_TABMASK    0x1FF000
#define PG_ADDR_PDPT(addr) ((addr) >> PG_ADDR_PDPTSHIFT)
#define PG_ADDR_DIR(addr)  (((addr) & (PG_ADDR_DIRMASK)) >> PG_ADDR_DIRSHIFT)
#define PG_ADDR_TAB(addr)  (((addr) & (PG_ADDR_TABMASK)) >> PG_ADDR_TABSHIFT)

// The address space
static pdpte_t* pdpt = NULL;

void NbCpuAsInit()
{
    // Grab PDPT from CR3
    pdpt = (pdpte_t*) NbReadCr3();
    assert (pdpt);
}

static pte_t* cpuAsAllocTab (pde_t* pdir, uintptr_t virt, uint64_t flags)
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

static pde_t* cpuAsAllocDir (uintptr_t virt)
{
    pde_t* dir = (pde_t*) NbFwAllocPage();
    if (!dir)
        return NULL;
    memset (dir, 0, NEXBOOT_CPU_PAGE_SIZE);
    // Map it
    pdpt[PG_ADDR_PDPT (virt)] = PT_P | (paddr_t) dir;
    // Reload PDPTE registers
    uint32_t dirp = NbReadCr3();
    NbWriteCr3 (dirp);
    return dir;
}

bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags)
{
    // Translate flags to CPU dependent flags
    uint64_t ptFlags = PT_P;
    if (flags & NB_CPU_AS_RW)
        ptFlags |= PT_RW;
    uint32_t pdptIdx = PG_ADDR_PDPT (virt);
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Check if a directory is mapped
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
        pdir = cpuAsAllocDir (virt);
        if (!pdir)
            return false;
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
        pgTab = cpuAsAllocTab (pdir, virt, ptFlags);
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
    NbInvlpg (virt);
    return true;
}

void NbCpuAsUnmap (uintptr_t virt)
{
    uint32_t pdptIdx = PG_ADDR_PDPT (virt);
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Check if a directory is mapped
    pdpte_t* pdpte = &pdpt[pdptIdx];
    if (!(*pdpte))
        return;
    pde_t* pdir = (pde_t*) PT_GETFRAME (*pdpte);
    pde_t* pde = &pdir[dirIdx];
    // Check if a table is mapped
    if (!(*pde))
        return;
    pte_t* pgTab = (pte_t*) PT_GETFRAME (*pde);
    pte_t* pte = &pgTab[tabIdx];
    *pte = 0;
    NbInvlpg (virt);
}

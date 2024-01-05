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

// FIXME: This module currently only supports 48-bit addresses, whereas
// ARMv8 theoretically allows 52-bit addresses. This is for simplicity's sake

// Virtual address manipulating macros
// Shift table for each level
static uint8_t idxShiftTab[] = {0, 12, 21, 30, 39, 48};

// Macro to get level
#define AS_IDX_MASK               0x1FF
#define AS_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (AS_IDX_MASK))

// Page entry flags
#define PT_V                   (1 << 0)
#define PT_PG                  (1 << 1)
#define PT_TAB                 (1 << 1)
#define PT_RO                  (1 << 7)
#define PT_XN                  (1 << 54)
#define PT_AF                  (1 << 10)
#define PT_FRAME               0xFFFFFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Value to mask with to get non-canonical address
#define AS_CANONICAL_MASK 0x0000FFFFFFFFFFFF

// General PTE type
typedef uint64_t pte_t;

// MMFR0 values
#define MMFR0_PABITS_MASK   0xF
#define MMFR0_4K_GRAN_SHIFT 28
#define MMFR0_4K_GRAN_MASK  0xF

// SCTLR values
#define SCTLR_MMU_ENABLE (1 << 0)
#define SCTLR_DATA_CACHE (1 << 2)
#define SCTLR_SP_ALIGN   (1 << 3)
#define SCTLR_SP_ALIGN0  (1 << 4)
#define SCTLR_INST_CACHE (1 << 12)
#define SCTLR_MMU_WXN    (1 << 19)
#define SCTLR_DATA_BE    (1 << 24)
#define SCTLR_TRANS_BE   (1 << 25)

// TCR values
#define TCR_EOPD1       (1ULL << 56)
#define TCR_DS          (1ULL << 52)
#define TCR_IPS         (7ULL << 32)
#define TCR_IPS_SHIFT   (32ULL)
#define TCR_TG1         (3 << 30)
#define TCR_TG1_SHIFT   (30)
#define TCR_SH1         (3 << 28)
#define TCR_SH1_SHIFT   (28)
#define TCR_ORGN1       (3 << 26)
#define TCR_ORGN1_SHIFT (26)
#define TCR_IRGN1       (3 << 24)
#define TCR_IRGN1_SHIFT (24)
#define TCR_EPD1        (1 << 23)
#define TCR_T1SZ        (0x3F << 16)
#define TCR_T1SZ_SHIFT  (16)
#define TCR_TG0         (3 << 14)
#define TCR_TG0_SHIFT   (14)
#define TCR_SH0         (3 << 12)
#define TCR_SH0_SHIFT   (12)
#define TCR_ORGN0       (3 << 10)
#define TCR_ORGN0_SHIFT (10)
#define TCR_IRGN0       (3 << 8)
#define TCR_IRGN0_SHIFT (8)
#define TCR_EPD0        (1 << 7)
#define TCR_T0SZ        (0x3F)
#define TCR_T0SZ_SHIFT  (0)

#define TTBR_REGION_SZ 16ULL

// EL we are running at
static int currentEl = 0;

// Page directory we are working on
static pte_t* pgBase = NULL;
static pte_t* pgBase2 = NULL;

static int asMaxLevel = 4;

// Initializes address space manager
void NbCpuAsInit()
{
    // Ensure 4k pages are supported
    uint8_t mask4k =
        (NbCpuReadMsr ("ID_AA64MMFR0_EL1") >> MMFR0_4K_GRAN_SHIFT) & MMFR0_4K_GRAN_MASK;
    if (mask4k == 0xF)
    {
        // Tell user
        NbLogMessage ("nexboot: fatal error: CPU doesn't support 4K pages\n",
                      NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    currentEl = NbCpuReadMsr ("CurrentEl") >> 2;
    // Ensure we are not in EL3 or EL0
    if (currentEl == 0 || currentEl == 3)
    {
        NbLogMessage ("nexboot: fatal error: not running in EL1 or EL2",
                      NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Set up paging structure
    pgBase = (pte_t*) NbFwAllocPage();
    assert (pgBase);
    pgBase2 = (pte_t*) NbFwAllocPage();
    assert (pgBase2);
}

// Gets paging entry as current level
static inline pte_t* cpuAsGetEntry (pte_t* curTab, uintptr_t addr, int level)
{
    int idx = AS_IDX_LEVEL (addr, level);
    return &curTab[idx];
}

// Allocates paging structure and puts it in specified level
static pte_t* cpuAsAllocSt (pte_t* curSt, uintptr_t addr, int level)
{
    pte_t* newSt = (pte_t*) NbFwAllocPage();
    if (!newSt)
        return NULL;
    memset (newSt, 0, NEXBOOT_CPU_PAGE_SIZE);
    // Map it
    curSt[AS_IDX_LEVEL (addr, level)] = (paddr_t) newSt | PT_PG | PT_V;
    return newSt;
}

bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags)
{
    uintptr_t ovirt = virt;
    // Translate flags to CPU dependent flags
    uint64_t ptFlags = PT_V | PT_AF | PT_PG | PT_RO;
    if (flags & NB_CPU_AS_RW)
        ptFlags &= ~(PT_RO);
    if (flags & NB_CPU_AS_WT)
        ptFlags |= (1 << 2);
    //  De-canonicalize the address
    pte_t* curSt = NULL;
    if (virt & (1ULL << 48))
    {
        virt &= AS_CANONICAL_MASK;
        curSt = pgBase;
    }
    else
    {
        curSt = pgBase2;
    }
    // Loop through each level
    for (int i = asMaxLevel; i > 1; --i)
    {
        // Grab entry
        pte_t* ent = cpuAsGetEntry (curSt, virt, i);
        // Check if we need to map a table into it
        if (*ent)
        {
            // Get structure
            curSt = (pte_t*) PT_GETFRAME (*ent);
        }
        else
        {
            curSt = cpuAsAllocSt (curSt, virt, i);
            if (!curSt)
                return false;
        }
    }
    // Grab last PML entry
    pte_t* lastEnt = cpuAsGetEntry (curSt, virt, 1);
    // Map it
    *lastEnt = phys | ptFlags;
    // Invalidate TLB
    asm volatile("dsb ishst; tlbi vae1, %0" : : "r"(ovirt >> 12));
    return true;
}

void NbCpuAsUnmap (uintptr_t virt)
{
    uintptr_t ovirt = virt;
    // Decanonicalize
    virt &= AS_CANONICAL_MASK;
    // Iterate through levels
    pte_t* curSt = pgBase;
    for (int i = asMaxLevel; i > 1; --i)
    {
        // Get entry
        pte_t* ent = cpuAsGetEntry (curSt, virt, i);
        if (!(*ent))
            return;                             // Address not actually mapped
        curSt = (pte_t*) PT_GETFRAME (*ent);    // Get structure
    }
    // Grab last PML entry
    pte_t* lastEnt = cpuAsGetEntry (curSt, virt, 1);
    *lastEnt = 0;    // Unmap
    asm volatile("dsb ishst; tlbi vae1, %0" : : "r"(ovirt >> 12));
}

// Enables paging
void NbCpuEnablePaging()
{
    uint64_t paBits = NbCpuReadMsr ("ID_AA64MMFR0_EL1") & MMFR0_PABITS_MASK;
    if (paBits == 6)
        paBits = 5;    // We don't support 52 bit addresses
    // Set up SCTLR
    uint64_t sctlrEl1 = NbCpuReadMsr ("SCTLR_EL1");
    // Clear what we don't want
    sctlrEl1 &= ~(SCTLR_DATA_BE | SCTLR_TRANS_BE);
    // Set what we want
    sctlrEl1 |=
        (SCTLR_DATA_CACHE | SCTLR_INST_CACHE | SCTLR_SP_ALIGN | SCTLR_SP_ALIGN0 | SCTLR_MMU_ENABLE);
    NbCpuWriteMsr ("SCTLR_EL1", sctlrEl1);
    uint64_t tcrEl1 = NbCpuReadMsr ("TCR_EL1");
    // Setup TTBR1
    tcrEl1 |= (TCR_EOPD1);
    tcrEl1 &= ~(TCR_EPD1);
    tcrEl1 |= (3ULL << TCR_SH1_SHIFT);
    tcrEl1 |= (1ULL << TCR_IRGN1_SHIFT);
    tcrEl1 |= (1ULL << TCR_ORGN1_SHIFT);
    tcrEl1 |= (TTBR_REGION_SZ << TCR_T1SZ_SHIFT);
    tcrEl1 |= (2ULL << TCR_TG1_SHIFT);
    tcrEl1 |= (paBits << TCR_IPS_SHIFT);
    // Setup TTBR0 if we are in EL2
    if (currentEl == 2)
    {
        tcrEl1 &= ~(TCR_EPD0);
        tcrEl1 |= (0ULL << TCR_TG0_SHIFT);
        tcrEl1 |= (3ULL << TCR_SH0_SHIFT);
        tcrEl1 |= (1ULL << TCR_IRGN0_SHIFT);
        tcrEl1 |= (1ULL << TCR_ORGN0_SHIFT);
        tcrEl1 |= (TTBR_REGION_SZ << TCR_T0SZ_SHIFT);
        NbCpuWriteMsr ("TTBR0_EL1", (uintptr_t) pgBase2 | (1 << 0));
    }
    NbCpuWriteMsr ("TCR_EL1", tcrEl1);
    NbCpuWriteMsr ("TTBR1_EL1", (uintptr_t) pgBase | (1 << 0));
}

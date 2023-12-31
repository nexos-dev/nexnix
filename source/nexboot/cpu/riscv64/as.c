/*
    as.c - contains address space management code
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
#include <string.h>

// SATP mode values
#define SATP_MODE_BARE  0
#define SATP_MODE_SV39  8
#define SATP_MODE_SV48  9
#define SATP_MODE_SV57  10
#define SATP_MODE_SHIFT 60ULL

typedef uint64_t pte_t;

// General paging macros
#define AS_PAGE_MASK 0xFFFFFFFFFFFFF000;
#define AS_PPN_SHIFT 12

// Virtual address manipulating macros
// Shift table for each level
static uint8_t idxShiftTab[] = {0, 12, 21, 30, 39, 48};

// Macro to get level
#define AS_IDX_MASK               0x1FF
#define AS_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (AS_IDX_MASK))

// Page table flags
#define PT_P             (1 << 0)
#define PT_R             (1 << 1)
#define PT_W             (1 << 2)
#define PT_X             (1 << 3)
#define PT_G             (1 << 5)
#define PT_FRAME         0x003ffffffffffc00
#define PT_GETFRAME(pte) ((pte & PT_FRAME) << 2)

// Canonicalizing variables
static uint64_t canonMask = 0;

// Paging mode we are in
static uint64_t pgMode = 0;
static int asMaxLevel = 0;

// Top level paging structure
static pte_t* pgBase = NULL;

// Initializes address space manager
void NbCpuAsInit()
{
    pgBase = (pte_t*) NbFwAllocPage();    // Allocate top level structure
    memset (pgBase, 0, NEXBOOT_CPU_PAGE_SIZE);
    // First we need to figure out the highest supported paging mode
    // Since we are in M-mode, we can do this easily. We simply write
    // SATP until successful
    uint64_t curMode = SATP_MODE_SV57;
    bool testSucceded = false;
    while (!testSucceded)
    {
        uint64_t satp = (curMode << SATP_MODE_SHIFT) | ((paddr_t) pgBase >> AS_PPN_SHIFT);
        NbCpuWriteCsr ("satp", satp);
        if (NbCpuReadCsr ("satp") == satp)
            testSucceded = true;    // Mode supported
        else
            --curMode;    // Move to next mode
    }
    NbCpuWriteCsr ("satp", 0);
    pgMode = curMode;
    // Set canonicalization mask
    if (pgMode == SATP_MODE_SV57)
        canonMask = 0x1FFFFFFFFFFFFFF;
    else if (pgMode == SATP_MODE_SV48)
        canonMask = 0xFFFFFFFFFFFF;
    else if (pgMode == SATP_MODE_SV39)
        canonMask = 0x7FFFFFFFFF;
    else
    {
        NbLogMessage ("nexboot: error: paging mode unsupported\r\n", NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    asMaxLevel = pgMode - 5;
}

// De-canonicalizes an address
static inline uintptr_t cpuAsDecanonical (uintptr_t addr)
{
    return addr & canonMask;
}

// Gets paging entry as current level
static inline pte_t* cpuAsGetEntry (pte_t* curTab, uintptr_t addr, int level)
{
    int idx = AS_IDX_LEVEL (addr, level);
    return &curTab[idx];
}

// Allocates paging structure and puts it in specified level
static inline pte_t* cpuAsAllocSt (pte_t* curSt, uintptr_t addr, int level)
{
    pte_t* newSt = (pte_t*) NbFwAllocPage();
    if (!newSt)
        return NULL;
    memset (newSt, 0, NEXBOOT_CPU_PAGE_SIZE);
    // Map it
    curSt[AS_IDX_LEVEL (addr, level)] = (((paddr_t) newSt) >> 2) | PT_P;
    return newSt;
}

// Maps address into space
bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags)
{
    // Translate flags to CPU dependent flags
    uint64_t ptFlags = PT_P | PT_R | PT_X;
    if (flags & NB_CPU_AS_RW)
        ptFlags |= PT_W;
    if (flags & NB_CPU_AS_NX)
        ptFlags &= ~(PT_X);
    //  De-canonicalize the address
    virt = cpuAsDecanonical (virt);
    // Loop through each level
    pte_t* curSt = pgBase;
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
    *lastEnt = (phys >> 2) | ptFlags;
    return true;
}

// Unmaps address from address space
void NbCpuAsUnmap (uintptr_t virt)
{
    // Decanonicalize
    virt = cpuAsDecanonical (virt);
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
}

// Enables paging
void NbCpuEnablePaging()
{
}

// Gets SATP
uint64_t NbCpuGetSatp()
{
    return (pgMode << SATP_MODE_SHIFT) | ((paddr_t) pgBase >> AS_PPN_SHIFT);
}

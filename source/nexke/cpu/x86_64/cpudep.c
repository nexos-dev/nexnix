/*
    cpudep.c - contains CPU dependent part of nexke
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
#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <string.h>

// Globals

// The system's CCB. A very important data structure that contains the kernel's
// deepest bowels
static NkCcb_t ccb = {0};    // The CCB

// The GDT
static CpuSegDesc_t cpuGdt[CPU_GDT_MAX];
// The IDT
static CpuIdtEntry_t cpuIdt[CPU_IDT_MAX];

// Free list of segments
static CpuFreeSeg_t* cpuSegFreeList = NULL;
static SlabCache_t* cpuSegCache = NULL;

// Sets up a GDT gate
static void cpuSetGdtGate (CpuSegDesc_t* desc,
                           uint32_t base,
                           uint32_t limit,
                           uint16_t flags,
                           int dpl,
                           int type)
{
    // Do the massive amounts of bit twidling needed for x86's weirdness
    desc->baseLow = base & 0xFFFF;
    desc->limitLow = limit & 0xFFFF;
    desc->baseMid = (base >> 16) & 0xFF;
    desc->baseHigh = (base >> 24) & 0xFF;
    // If this is not a system segment, ensure type is 0
    if (flags & CPU_SEG_NON_SYS && type)
        NkPanic ("nexke: error: attempted to install malformed GDT entry");
    // Set up the flags field. Note that this incorporates the limit high part
    desc->flags = flags | CPU_SEG_PRESENT | type | (dpl << CPU_SEG_DPL_SHIFT);
    // Throw limit in there
    desc->flags |= ((limit >> 16) & 0xF) << CPU_SEG_LIMIT_SHIFT;
}

// Allocates a segment for a TSS. Returns segment number
int CpuAllocSeg (uintptr_t base, uintptr_t limit, int dpl)
{
    // Grab segment from free list
    CpuFreeSeg_t* seg = cpuSegFreeList;
    if (!seg)
    {
        // System is out of segments
        return 0;
    }
    cpuSegFreeList = seg->next;
    // Set up gate
    cpuSetGdtGate (&cpuGdt[seg->segNum], base, limit, CPU_SEG_WRITABLE | CPU_SEG_NON_SYS, dpl, 0);
    // Free segment data structure
    int segNum = seg->segNum;
    MmCacheFree (cpuSegCache, seg);
    return segNum;
}

// Frees a segment
void CpuFreeSeg (int segNum)
{
    memset (&cpuGdt[segNum], 0, sizeof (CpuSegDesc_t));
    // Put on free list
    CpuFreeSeg_t* freeSeg = MmCacheAlloc (cpuSegCache);
    freeSeg->segNum = segNum;
    freeSeg->next = cpuSegFreeList;
    cpuSegFreeList = freeSeg;
}

// Sets up GDT
static void cpuInitGdt()
{
    // Set up segment free list
    cpuSegCache = MmCacheCreate (sizeof (CpuFreeSeg_t), NULL, NULL);
    assert (cpuSegCache);
    // Set up null segment
    cpuSetGdtGate (&cpuGdt[0], 0, 0, 0, 0, 0);
    // Set up kernel code
    cpuSetGdtGate (&cpuGdt[1],
                   0,
                   0xFFFFFFFF,
                   CPU_SEG_LONG | CPU_SEG_GRAN | CPU_SEG_CODE | CPU_SEG_READABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_KERNEL,
                   0);
    // Kernel data
    cpuSetGdtGate (&cpuGdt[2],
                   0,
                   0xFFFFFFFF,
                   CPU_SEG_DB | CPU_SEG_GRAN | CPU_SEG_WRITABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_KERNEL,
                   0);
    // User code
    cpuSetGdtGate (&cpuGdt[3],
                   0,
                   0xFFFFFFFF,
                   CPU_SEG_LONG | CPU_SEG_GRAN | CPU_SEG_CODE | CPU_SEG_READABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_USER,
                   0);
    // User data
    cpuSetGdtGate (&cpuGdt[4],
                   0,
                   0xFFFFFFFF,
                   CPU_SEG_DB | CPU_SEG_GRAN | CPU_SEG_WRITABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_USER,
                   0);
    // Prepare free list
    for (int i = 6; i < CPU_GDT_MAX; ++i)
    {
        // Prepare a free segment struct
        CpuFreeSeg_t* freeSeg = MmCacheAlloc (cpuSegCache);
        freeSeg->segNum = i;
        freeSeg->next = cpuSegFreeList;
        cpuSegFreeList = freeSeg;
    }
    // Load new GDT into CPU
    CpuTabPtr_t gdtr = {.base = (uintptr_t) cpuGdt, .limit = NEXKE_CPU_PAGESZ - 1};
    CpuFlushGdt (&gdtr);
}

// Sets up IDT gate
static void cpuSetIdtGate (CpuIdtEntry_t* gate,
                           uintptr_t handler,
                           uint8_t type,
                           uint8_t dpl,
                           uint8_t seg,
                           uint8_t ist)
{
    // Set it up
    gate->baseLow = handler & 0xFFFF;
    gate->baseMid = (handler >> 16) & 0xFFFF;
    gate->baseHigh = (handler >> 32) & 0xFFFFFFFF;
    gate->ist = ist;
    gate->seg = seg;
    gate->flags = type | (dpl << CPU_IDT_DPL_SHIFT) | CPU_IDT_PRESENT;
}

// Sets up IDT
static void cpuInitIdt()
{
    // Set up each handler
    for (int i = 0; i < CPU_IDT_MAX; ++i)
    {
        if (i == CPU_SYSCALL_INT || i == 1 || i == 3 || i == 4 || i == 5)
            cpuSetIdtGate (&cpuIdt[i],
                           CPU_GETTRAP (i),
                           CPU_IDT_TRAP,
                           3,
                           CPU_SEG_KCODE,
                           0);    // System call is trap
        else
            cpuSetIdtGate (&cpuIdt[i],
                           CPU_GETTRAP (i),
                           CPU_IDT_INT,
                           0,
                           CPU_SEG_KCODE,
                           0);    // Normal
                                  // case
    }
    // Install IDT
    CpuTabPtr_t idtPtr = {.base = (uintptr_t) cpuIdt, .limit = (CPU_IDT_MAX * 16) - 1};
    CpuInstallIdt (&idtPtr);
    // Setup GDT and IDT pointers
    ccb.archCcb.gdt = cpuGdt;
    ccb.archCcb.idt = cpuIdt;
    // Setup CR0, CR4, and EFER
    uint64_t cr0 = CpuReadCr0();
    cr0 |= (CPU_CR0_WP | CPU_CR0_AM);
    CpuWriteCr0 (cr0);
    uint64_t cr4 = CpuReadCr4();
    // Setup PSE, MCE, PGE, SSE, and SMEP
    if (CpuGetFeatures() & CPU_FEATURE_PSE)
        cr4 |= CPU_CR4_PSE;
    if (CpuGetFeatures() & CPU_FEATURE_MCE)
        cr4 |= CPU_CR4_MCE;
    if (CpuGetFeatures() & CPU_FEATURE_PGE)
        cr4 |= CPU_CR4_PGE;
    if (CpuGetFeatures() & CPU_FEATURE_SSE || CpuGetFeatures() & CPU_FEATURE_SSE2 ||
        CpuGetFeatures() & CPU_FEATURE_SSE3)
    {
        cr4 |= (CPU_CR4_OSFXSR | CPU_CR4_OSXMMEXCPT);
    }
    if (CpuGetFeatures() & CPU_FEATURE_SMEP)
        cr4 |= CPU_CR4_SMEP;
    CpuWriteCr4 (cr4);
    // Set EFER
    if (CpuGetFeatures() & CPU_FEATURE_MSR)
    {
        if (CpuGetFeatures() & CPU_FEATURE_XD)
        {
            // Enable NX
            uint64_t efer = CpuRdmsr (CPU_EFER_MSR);
            efer |= CPU_EFER_NXE;
            CpuWrmsr (CPU_EFER_MSR, efer);
        }
    }
}

// Prepares CCB data structure. This is the first thing called during boot
void CpuInitCcb()
{
    NkLogDebug ("nexke: Initializing CCB\n");
    // Grab boot info
    NexNixBoot_t* bootInfo = NkGetBootArgs();
    // Set up basic fields
    ccb.cpuArch = NEXKE_CPU_X86_64;
    ccb.cpuFamily = NEXKE_CPU_FAMILY_X86;
#ifdef NEXNIX_BOARD_PC
    ccb.sysBoard = NEXKE_BOARD_PC;
#else
#error Unrecognized board
#endif
    strcpy (ccb.sysName, bootInfo->sysName);
    // Detect CPUID features
    CpuDetectCpuid (&ccb);
    // Initialize GDT
    cpuInitGdt();
    // Initialize IDT
    cpuInitIdt();
    // Setup GDT and IDT pointers
    ccb.archCcb.gdt = cpuGdt;
    ccb.archCcb.idt = cpuIdt;
    // Setup CR0, CR4, and EFER
    uint64_t cr0 = CpuReadCr0();
    cr0 |= (CPU_CR0_WP | CPU_CR0_AM);
    CpuWriteCr0 (cr0);
    uint64_t cr4 = CpuReadCr4();
    // Setup PSE, MCE, PGE, SSE, and SMEP
    if (CpuGetFeatures() & CPU_FEATURE_PSE)
        cr4 |= CPU_CR4_PSE;
    if (CpuGetFeatures() & CPU_FEATURE_MCE)
        cr4 |= CPU_CR4_MCE;
    if (CpuGetFeatures() & CPU_FEATURE_PGE)
        cr4 |= CPU_CR4_PGE;
    if (CpuGetFeatures() & CPU_FEATURE_SSE || CpuGetFeatures() & CPU_FEATURE_SSE2 ||
        CpuGetFeatures() & CPU_FEATURE_SSE3)
    {
        cr4 |= (CPU_CR4_OSFXSR | CPU_CR4_OSXMMEXCPT);
    }
    if (CpuGetFeatures() & CPU_FEATURE_SMEP)
        cr4 |= CPU_CR4_SMEP;
    CpuWriteCr4 (cr4);
    // Set EFER
    if (CpuGetFeatures() & CPU_FEATURE_MSR)
    {
        if (CpuGetFeatures() & CPU_FEATURE_XD)
        {
            // Enable NX
            uint64_t efer = CpuRdmsr (CPU_EFER_MSR);
            efer |= CPU_EFER_NXE;
            CpuWrmsr (CPU_EFER_MSR, efer);
        }
    }
}

// Gets feature bits
uint64_t CpuGetFeatures()
{
    return ccb.archCcb.features;
}

// Returns CCB to caller
NkCcb_t* CpuGetCcb()
{
    return &ccb;
}

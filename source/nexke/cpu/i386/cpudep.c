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
// Double fault TSS
static CpuTss_t cpuDfaultTss = {0};

// Segment resource
static NkResArena_t* cpuSegs = NULL;

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

// Sets up IDT gate
static void cpuSetIdtGate (CpuIdtEntry_t* gate,
                           uintptr_t handler,
                           uint8_t type,
                           uint8_t dpl,
                           uint8_t seg)
{
    // Set it up
    gate->baseLow = handler & 0xFFFF;
    gate->baseHigh = (handler >> 16) & 0xFFFF;
    gate->resvd = 0;
    gate->seg = seg;
    gate->flags = type | (dpl << CPU_IDT_DPL_SHIFT) | CPU_IDT_PRESENT;
}

// Sets up GDT
static void cpuInitGdt()
{
    // Set up segment resource
    cpuSegs = NkCreateResource ("CpuSeg", 7, 8192 - 1);
    assert (cpuSegs);
    // Set up null segment
    cpuSetGdtGate (&cpuGdt[0], 0, 0, 0, 0, 0);
    // Set up kernel code
    cpuSetGdtGate (&cpuGdt[1],
                   0,
                   0xFFFFFFFF,
                   CPU_SEG_DB | CPU_SEG_GRAN | CPU_SEG_CODE | CPU_SEG_READABLE | CPU_SEG_NON_SYS,
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
                   CPU_SEG_DB | CPU_SEG_GRAN | CPU_SEG_CODE | CPU_SEG_READABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_USER,
                   0);
    // User data
    cpuSetGdtGate (&cpuGdt[4],
                   0,
                   0xFFFFFFFF,
                   CPU_SEG_DB | CPU_SEG_GRAN | CPU_SEG_WRITABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_USER,
                   0);
    // Set kernel GS
    cpuSetGdtGate (&cpuGdt[CPU_CCB_SEG / 8],
                   (uint32_t) CpuRealCcb(),
                   sizeof (NkCcb_t),
                   CPU_SEG_DB | CPU_SEG_WRITABLE | CPU_SEG_NON_SYS,
                   CPU_DPL_KERNEL,
                   0);
    // Load new GDT into CPU
    CpuTabPtr_t gdtr = {.base = (uint32_t) cpuGdt, .limit = NEXKE_CPU_PAGESZ - 1};
    CpuFlushGdt (&gdtr);
}

// Sets up IDT
static void cpuInitIdt()
{
    // Set up each handler
    for (int i = 0; i < CPU_IDT_MAX; ++i)
    {
        if (i <= CPU_SYSCALL_INT && i != 8)
            cpuSetIdtGate (&cpuIdt[i],
                           CPU_GETTRAP (i),
                           CPU_IDT_TRAP,
                           3,
                           CPU_SEG_KCODE);    // Exception and system calls are traps
        else if (i == 8)
            cpuSetIdtGate (&cpuIdt[i], 0, CPU_IDT_TASK, 0, CPU_DFAULT_TSS);    // Double fault is
                                                                               // task gate
        else
            cpuSetIdtGate (&cpuIdt[i], CPU_GETTRAP (i), CPU_IDT_INT, 0, CPU_SEG_KCODE);    // Normal
                                                                                           // case
    }
    // Install IDT
    CpuTabPtr_t idtPtr = {.base = (uintptr_t) cpuIdt, .limit = (CPU_IDT_MAX * 8) - 1};
    CpuInstallIdt (&idtPtr);
}

// Allocates a segment for a data structure. Returns segment number
int CpuAllocSeg (uintptr_t base, uintptr_t limit, int dpl)
{
    // Allocate a segment ID
    int segNum = NkAllocResource (cpuSegs);
    // Set up gate
    cpuSetGdtGate (&cpuGdt[segNum], base, limit, CPU_SEG_WRITABLE | CPU_SEG_NON_SYS, dpl, 0);
    return segNum;
}

// Frees a segment
void CpuFreeSeg (int segNum)
{
    memset (&cpuGdt[segNum], 0, sizeof (CpuSegDesc_t));
    // Free it
    NkFreeResource (cpuSegs, segNum);
}

// Prepares CCB data structure
void CpuInitCcb()
{
    memset (&ccb, 0, sizeof (NkCcb_t));
    // Grab boot info
    NexNixBoot_t* bootInfo = NkGetBootArgs();
    // Set up basic fields
    ccb.self = &ccb;
    ccb.cpuArch = NEXKE_CPU_I386;
    ccb.cpuFamily = NEXKE_CPU_FAMILY_X86;
#ifdef NEXNIX_BOARD_PC
    ccb.sysBoard = NEXKE_BOARD_PC;
#else
#error Unrecognized board for i386
#endif
    ccb.archCcb.intsHeld = true;    // Hold interrupts
    ccb.archCcb.intRequested = true;
    strcpy (ccb.sysName, bootInfo->sysName);
    // Detect CPUID features. First figure out if CPUID is even available
    if (!CpuCheckCpuid())
    {
        ccb.archCcb.physAddrBits = 32, ccb.archCcb.virtAddrBits = 32;
        ccb.archCcb.vendor = CPU_VENDOR_INTEL;    // Assumption
        // Determine if this is a 386 or a 486
        if (CpuCheck486())
        {
            ccb.archCcb.features |= (CPU_FEATURE_INVLPG | CPU_FEATURE_AC);
            ccb.archCcb.family = 4;    // Set family for 486
        }
        else
            ccb.archCcb.family = 3;
        // Check for an FPU
        if (CpuCheckFpu())
        {
            ccb.archCcb.features |= CPU_FEATURE_FPU;
        }
    }
    else
    {
        CpuDetectCpuid (&ccb);
        ccb.archCcb.features |= CPU_FEATURE_INVLPG | CPU_FEATURE_AC;
    }
    // Initialize GDT
    cpuInitGdt();
    // Initialize IDT
    cpuInitIdt();
    // Set up double fault TSS in GDT
    cpuSetGdtGate (&cpuGdt[CPU_DFAULT_TSS / 8],
                   (uint32_t) &cpuDfaultTss,
                   sizeof (CpuTss_t),
                   0,
                   CPU_DPL_KERNEL,
                   CPU_SEG_TSS);
    // Set double fault task state
    cpuDfaultTss.cr3 = CpuReadCr3();
    cpuDfaultTss.eip = CPU_GETTRAP (8);
    cpuDfaultTss.eflags = 2;
    cpuDfaultTss.esp = (uint32_t) MmAllocKvPage();
    cpuDfaultTss.ss = CPU_SEG_KDATA;
    cpuDfaultTss.cs = CPU_SEG_KCODE;
    // Setup GDT and IDT pointers
    ccb.archCcb.gdt = cpuGdt;
    ccb.archCcb.idt = cpuIdt;
    // Setup CR0, CR4, and EFER
    uint32_t cr0 = CpuReadCr0();
    if (CpuGetFeatures() & CPU_FEATURE_AC)
        cr0 |= (CPU_CR0_WP | CPU_CR0_AM);    // AC implise WP
    CpuWriteCr0 (cr0);
    if (ccb.archCcb.family > 4)
    {
        uint32_t cr4 = CpuReadCr4();
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
    }
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
NkCcb_t* CpuRealCcb()
{
    return &ccb;
}

// Allocates a kernel stack
static void* cpuAllocKstack()
{
    // Allocate it
    void* stack = MmAllocKvRegion ((CPU_KSTACK_SZ >> NEXKE_CPU_PAGE_SHIFT) + 2, 0);
    // Create two guard pages
    MmPage_t* guard1 = MmAllocGuardPage();
    MmPage_t* guard2 = MmAllocGuardPage();
    if (!guard1 || !guard2 || !stack)
        return NULL;
    // Add them
    MmObject_t* kobj = MmGetKernelObject();
    MmAddPage (kobj, (size_t) stack - MmGetKernelSpace()->startAddr, guard1);
    uintptr_t stackEnd = (CPU_KSTACK_SZ + NEXKE_CPU_PAGESZ) + (uintptr_t) stack;
    MmAddPage (kobj, stackEnd - MmGetKernelSpace()->startAddr, guard2);
    return stack + NEXKE_CPU_PAGESZ;    // Return non-guard page
}

// Destroys kernel stack
static void cpuDestroyKstack (void* stack)
{
    // Free region taking into account guard page
    MmFreeKvRegion (stack - NEXKE_CPU_PAGESZ);
}

// Allocates a CPU context and intializes it
// On i386, a CPU's context is it's kernel stack
CpuContext_t* CpuAllocContext (uintptr_t entry)
{
    // Allocate a stack
    void* stack = cpuAllocKstack();
    if (!stack)
        return NULL;
    CpuContext_t* context = (CpuContext_t*) (stack + CPU_KSTACK_SZ - sizeof (CpuContext_t));
    // Initialize it
    context->ebx = 0;
    context->edi = 0;
    context->esi = 0;
    context->ebp = 0;
    context->eip = entry;
    return context;
}

// Destroys a context
void CpuDestroyContext (CpuContext_t* context)
{
    // Get stack
    void* stack = (void*) (CpuPageAlignUp ((uintptr_t) context)) - CPU_KSTACK_SZ;
    cpuDestroyKstack (stack);
}

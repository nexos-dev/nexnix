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
// static CpuIdtEntry_t cpuIdt[CPU_IDT_MAX];

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

// Sets up GDT
static void cpuInitGdt()
{
}

// Prepares CCB data structure. This is the first thing called during boot
void CpuInitCcb()
{
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
    NkLogDebug ("nexke: Initializing CCB\n");
    // Detect CPUID features
    CpuDetectCpuid (&ccb);
    // Initialize GDT
    cpuInitGdt();
}

// Returns CCB to caller
NkCcb_t* CpuGetCcb()
{
    return &ccb;
}

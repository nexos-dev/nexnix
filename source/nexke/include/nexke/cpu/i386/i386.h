/*
    i386.h - contains nexke i386 stuff
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

#ifndef _I386_H
#define _I386_H

#include <stdint.h>

// CR functions
uint32_t CpuReadCr0();
void CpuWriteCr0 (uint32_t val);
uint32_t CpuReadCr3();
void CpuWriteCr3 (uint32_t val);
uint32_t CpuReadCr4();
void CpuWriteCr4 (uint32_t val);
uint32_t CpuReadCr2();

// Physical address type
#ifdef NEXNIX_I386_PAE
typedef uint64_t paddr_t;
#else
typedef uint32_t paddr_t;
#endif

#define NEXKE_KERNEL_BASE 0xC0000000

// PFN map base address
#define NEXKE_PFNMAP_BASE 0xC8040000
#define NEXKE_PFNMAP_MAX  0x8000000

// Max memory
#ifndef NEXNIX_I386_PAE
#define NEXKE_MAX_PAGES 0x100000
#endif

// Addresses
#define NEXKE_USER_ADDR_END     0xBFFFFFFF
#define NEXKE_KERNEL_ADDR_START 0xD0040000
#define NEXKE_KV_ADDR_END       0xDEFFFFFF
#define NEXKE_MMIO_ADDR_START   0xDF000000
#define NEXKE_KERNEL_ADDR_END   0xDFFFFFFF

#define NEXKE_KERNEL_DIRBASE 0xD003F000

// Framebuffer locations
#define NEXKE_FB_BASE      0xF0000000
#define NEXKE_BACKBUF_BASE 0xE0000000

// IDT gate
typedef struct _x86idtent
{
    uint16_t baseLow;    // Low part of handler base
    uint16_t seg;        // CS value for interrupt
    uint8_t resvd;
    uint8_t flags;        // Flags of interrupt
    uint16_t baseHigh;    // High 16 of base
} __attribute__ ((packed)) CpuIdtEntry_t;

// Double fault TSS segment
#define CPU_DFAULT_TSS 0x28

// TSS structure
typedef struct _i386tss
{
    uint16_t backLink;    // Previously executing task
    uint16_t resvd0;
    uint32_t esp0;    // Ring 0 stack
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    // This stuff is unused on NexNix
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldtSeg;
    uint16_t resvd;
    uint16_t iobp;    // IOPB offset
    uint32_t ssp;
} CpuTss_t;

#include <nexke/cpu/i386/mul.h>
#include <nexke/cpu/ptab.h>
#include <nexke/cpu/x86/x86.h>

// Interrupt context
typedef struct _icontext
{
    uint32_t es, ds;                                       // Segments
    uint32_t edi, esi, ebp, unused, ebx, edx, ecx, eax;    // GPRs
    uint32_t intNo, errCode, eip, cs, eflags, esp, ss;
} CpuIntContext_t;

#define CPU_CTX_INTNUM(ctx) ((ctx)->intNo)

// Checks if CPUID exists
bool CpuCheckCpuid();

// Check if this is a 486
bool CpuCheck486();

// Checks if an FPU exists
bool CpuCheckFpu();

#endif

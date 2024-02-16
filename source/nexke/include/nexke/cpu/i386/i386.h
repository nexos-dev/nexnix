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

// Physical address type
#ifdef NEXNIX_I386_PAE
typedef uint64_t paddr_t;
#else
typedef uint32_t paddr_t;
#endif

// PFN map base address
#define NEXKE_PFNMAP_BASE 0xC8040000
#define NEXKE_PFNMAP_MAX  0x8000000

// User address end
#define NEXKE_USER_ADDR_END     0xAFFFFFFF
#define NEXKE_KERNEL_ADDR_START 0xD0040000

#define NEXKE_KERNEL_ADDR_END 0xDFFFFFFF

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

// Flags
#define CPU_IDT_INT       0xF
#define CPU_IDT_TRAP      0xE
#define CPU_IDT_TASK      5
#define CPU_IDT_PRESENT   (1 << 7)
#define CPU_IDT_DPL_SHIFT 5

// Double fault TSS segment
#define CPU_DFAULT_TSS 0x28

#include <nexke/cpu/x86/x86.h>

// Interrupt context
typedef struct _icontext
{
    uint32_t es, ds;                                       // Segments
    uint32_t edi, esi, ebp, unused, ebx, edx, ecx, eax;    // GPRs
    uint32_t intNo, errCode, eip, cs, eflags, esp, ss;
} CpuIntContext_t;

#define CPU_CTX_INTNUM(ctx) ((ctx)->intNo)

#endif

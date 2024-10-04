/*
    cpu.h - contains CPU specific abstractions
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

#ifndef _NB_CPU_H
#define _NB_CPU_H

#include <stdint.h>

// CPU constants
#define NEXBOOT_CPU_PAGE_SIZE 0x1000

static inline uint64_t NbPageAlignUp (uint64_t ptr)
{
    // Dont align if already aligned
    if ((ptr & (NEXBOOT_CPU_PAGE_SIZE - 1)) == 0)
        return ptr;
    else
    {
        // Clear low 12 bits and them round to next page
        ptr &= ~(NEXBOOT_CPU_PAGE_SIZE - 1);
        ptr += NEXBOOT_CPU_PAGE_SIZE;
    }
    return ptr;
}

static inline uint64_t NbPageAlignDown (uint64_t ptr)
{
    return ptr & ~(NEXBOOT_CPU_PAGE_SIZE - 1);
}

// CPU flags register
#define NEXBOOT_CPU_CARRY_FLAG (1 << 0)

/// Halts system
void NbCrash();

/// Small I/O delay
void NbIoWait();

void NbOutb (uint16_t port, uint8_t val);
void NbOutw (uint16_t port, uint16_t val);
void NbOutl (uint16_t port, uint32_t val);

uint8_t NbInb (uint16_t port);
uint16_t NbInw (uint16_t port);
uint32_t NbInl (uint16_t port);

uint64_t NbReadCr0();
void NbWriteCr0 (uint64_t val);
uint64_t NbReadCr3();
void NbWriteCr3 (uint64_t val);
uint64_t NbReadCr4();
void NbWriteCr4 (uint64_t val);

void NbWrmsr (uint32_t msr, uint64_t val);
uint64_t NbRdmsr (uint32_t msr);

void NbInvlpg (uintptr_t addr);

// CR0 bits
#define NB_CR0_PE (1 << 0)
#define NB_CR0_WP (1 << 16)
#define NB_CR0_PG (1 << 31)

// CR4 bits
#define NB_CR4_PSE  (1 << 4)
#define NB_CR4_PAE  (1 << 5)
#define NB_CR4_PGE  (1 << 7)
#define NB_CR4_LA57 (1 << 12)

// EFER bits
#define NB_EFER_MSR 0xC0000080
#define NB_EFER_NXE (1 << 11)

#define NB_KE_STACK_BASE 0xFFFFFFFFCFFF0000

typedef uint64_t paddr_t;

void NbCpuLaunchKernel (uintptr_t entry, uintptr_t bootInf);

#endif

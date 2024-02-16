/*
    armv8.h - contains nexke armv8 stuff
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

#ifndef _ARMV8_H
#define _ARMV8_H

#include <stdint.h>

typedef uint64_t paddr_t;

typedef struct _nkarchccb
{
    uint64_t features;    // CPU feature flags
} NkArchCcb_t;

void __attribute__ ((noreturn)) CpuCrash();

// CPU page size
#define NEXKE_CPU_PAGESZ 0x1000

// PFN map base
#define NEXKE_PFNMAP_BASE 0xFFFFFFFD00000000
#define NEXKE_PFNMAP_MAX  0xF7FFFF000

// User address end
#define NEXKE_USER_ADDR_END 0x7FFFFFFFFFFF
// Kernel general allocation start
#define NEXKE_KERNEL_ADDR_START 0xFFFFFFFFC0000000
#define NEXKE_KERNEL_ADDR_END   0xFFFFFFFFDFFFFFFF

// Framebuffer locations
#define NEXKE_FB_BASE      0xFFFFFFFFF0000000
#define NEXKE_BACKBUF_BASE 0xFFFFFFFFE0000000

#define NEXKE_SERIAL_MMIO_BASE 0xFFFFFFFF90000000

// MSR functions
#define CpuReadMsr(msr)                             \
    ({                                              \
        uint64_t __tmp = 0;                         \
        asm volatile("mrs %0, " msr : "=r"(__tmp)); \
        __tmp;                                      \
    })

#define CpuWriteMsr(msr, val) asm volatile("msr " msr ", %0" : : "r"(val));

#endif

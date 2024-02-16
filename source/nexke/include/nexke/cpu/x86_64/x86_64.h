/*
    x86_64.h - contains nexke i386 stuff
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

#ifndef _X86_64_H
#define _X86_64_H

#include <nexke/cpu/x86/x86.h>
#include <stdint.h>

// CR functions
uint64_t CpuReadCr0();
void CpuWriteCr0 (uint64_t val);
uint64_t CpuReadCr3();
void CpuWriteCr3 (uint64_t val);
uint64_t CpuReadCr4();
void CpuWriteCr4 (uint64_t val);

// User address end
#ifndef NEXNIX_X86_64_LA57
#define NEXKE_USER_ADDR_END 0x7FFFFFFFFFFF
#else
#define NEXKE_USER_ADDR_END 0xFFFFFFFFFFFFFF
#endif

// Kernel general allocation start
#define NEXKE_KERNEL_ADDR_START 0xFFFFFFFFC0000000
#define NEXKE_KERNEL_ADDR_END   0xFFFFFFFFDFFFFFFF

// Framebuffer locations
#define NEXKE_FB_BASE      0xFFFFFFFFF0000000
#define NEXKE_BACKBUF_BASE 0xFFFFFFFFE0000000

// PFN map base
#define NEXKE_PFNMAP_BASE 0xFFFFFFFD00000000
#define NEXKE_PFNMAP_MAX  0xF7FFFFFF0

typedef uint64_t paddr_t;

#endif

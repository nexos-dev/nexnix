/*
    cpu.h - contains CPU related kernel functions
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

#ifndef _CPU_H
#define _CPU_H

// Include arch header. This makes use of computed includes
#include NEXKE_ARCH_HEADER

// CCB structure (aka CPU control block)
// This is the core data structure for the CPU, and hence, the kernel
typedef struct _nkccb
{
    int cpuArch;      // CPU architecture
    int cpuFamily;    // Architecture family
    int sysBoard;     // System hardware / SOC type
    char sysName[64];
    //NkArchCcb_t archCcb;    // Architecture dependent part of CCB
} NkCcb_t;

// Defined CPU architectures
#define NEXKE_CPU_I386   1
#define NEXKE_CPU_X86_64 2

// Defined CPU familys
#define NEXKE_CPU_FAMILY_X86 1

// Defined boards
#define NEXKE_BOARD_PC 1

// Initializes CPU control block
void CpuInitCcb();

// Returns CCB to caller
NkCcb_t* CpuGetCcb();

#endif

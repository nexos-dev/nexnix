/*
    cpu.h - contains CPU specific abstractions
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

#endif

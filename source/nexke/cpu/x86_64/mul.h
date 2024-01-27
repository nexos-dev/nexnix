/*
    mul.h - contains MUL header
    Copyright 2024 The NexNix Project

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

#ifndef _MUL_H
#define _MUL_H

#include <stdint.h>

// Basic types
typedef uint64_t pmle_t;

// Page table flags
#define PF_P                   (1ULL << 0)
#define PF_RW                  (1ULL << 1)
#define PF_US                  (1ULL << 2)
#define PF_WT                  (1ULL << 3)
#define PF_CD                  (1ULL << 4)
#define PF_A                   (1ULL << 5)
#define PF_D                   (1ULL << 6)
#define PF_PS                  (1ULL << 7)
#define PF_PAT                 (1ULL << 7)
#define PF_G                   (1ULL << 8)
#define PF_PSPAT               (1ULL << 12)
#define PF_NX                  (1ULL << 63)
#define PT_FRAME               0x7FFFFFFFFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Virtual address manipulating macros
// Shift table for each level
static uint8_t idxShiftTab[] = {0, 12, 21, 30, 39, 48};

// Macro to get level index
#define MUL_IDX_MASK               0x1FF
#define MUL_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (MUL_IDX_MASK))

// Canonicalizing functions

// According to the Intel manual, bits 63:48 in an address must equal bit 47
// However, in order to index page tables, bits 63:48 must be clear
// Note, however, with LA57, instead of bits 63:48, it is bits 63:56

#ifdef NEXNIX_X86_64_LA57
#define MUL_TOP_ADDR_BIT   (1ULL << 56)
#define MUL_CANONICAL_VAL  0xFE00000000000000
#define MUL_CANONICAL_MASK 0x1FFFFFFFFFFFFFF
#else
#define MUL_TOP_ADDR_BIT   (1ULL << 47)
#define MUL_CANONICAL_VAL  0xFFFF000000000000
#define MUL_CANONICAL_MASK 0x0000FFFFFFFFFFFF
#endif

#endif
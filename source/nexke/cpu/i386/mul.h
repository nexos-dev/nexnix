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

#ifdef NEXNIX_I386_PAE

// Basic types
typedef uint64_t pdpte_t;
typedef uint64_t pde_t;
typedef uint64_t pte_t;

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

// Virtual address management macros
#define PG_ADDR_PDPTSHIFT  30
#define PG_ADDR_DIRSHIFT   21
#define PG_ADDR_DIRMASK    0x3FE00000
#define PG_ADDR_TABSHIFT   12
#define PG_ADDR_TABMASK    0x1FF000
#define PG_ADDR_PDPT(addr) ((addr) >> PG_ADDR_PDPTSHIFT)
#define PG_ADDR_DIR(addr)  (((addr) & (PG_ADDR_DIRMASK)) >> PG_ADDR_DIRSHIFT)
#define PG_ADDR_TAB(addr)  (((addr) & (PG_ADDR_TABMASK)) >> PG_ADDR_TABSHIFT)

#else

// Basic types
typedef uint32_t pde_t;
typedef uint32_t pte_t;

// Page table flags
#define PF_P                   (1 << 0)
#define PF_RW                  (1 << 1)
#define PF_US                  (1 << 2)
#define PF_WT                  (1 << 3)
#define PF_CD                  (1 << 4)
#define PF_A                   (1 << 5)
#define PF_D                   (1 << 6)
#define PF_PS                  (1 << 7)
#define PF_PAT                 (1 << 7)
#define PF_G                   (1 << 8)
#define PF_PSPAT               (1 << 12)
#define PT_FRAME               0xFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Virtual address management macros
#define PG_ADDR_DIRSHIFT       22
#define PG_ADDR_TABSHIFT       12
#define PG_ADDR_TABMASK        0x3FF000
#define PG_ADDR_DIR(addr)      ((addr) >> PG_ADDR_DIRSHIFT)
#define PG_ADDR_TAB(addr)      (((addr) & (PG_ADDR_TABMASK)) >> PG_ADDR_TABSHIFT)

#endif

#endif

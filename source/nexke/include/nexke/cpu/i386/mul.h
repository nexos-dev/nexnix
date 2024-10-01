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

#include <stdbool.h>
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
#define PT_FRAME               0x7FFFFFFFFFFFF000ULL
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

// Shift table for each level for arch independent layer
static uint8_t idxShiftTab[] = {0, 12, 21};
// Priority of levels in cache
static bool idxPrioTab[] = {false, false, true};

// Macro to get level index
#define MUL_IDX_MASK               0x1FF
#define MUL_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (MUL_IDX_MASK))
#define MUL_IDX_PRIO(level)        (idxPrioTab[level])

#define MmMulFlushCacheEntry MmMulFlush

#else

// Basic types
typedef uint32_t pde_t;
typedef uint32_t pte_t;

// Page table flags
#define PF_P                       (1 << 0)
#define PF_RW                      (1 << 1)
#define PF_US                      (1 << 2)
#define PF_WT                      (1 << 3)
#define PF_CD                      (1 << 4)
#define PF_A                       (1 << 5)
#define PF_D                       (1 << 6)
#define PF_PS                      (1 << 7)
#define PF_PAT                     (1 << 7)
#define PF_G                       (1 << 8)
#define PF_PSPAT                   (1 << 12)
#define PT_FRAME                   0xFFFFF000
#define PT_GETFRAME(pt)            ((pt) & (PT_FRAME))
#define PT_SETFRAME(pt, frame)     ((pt) |= ((frame) & (PT_FRAME)))

// Virtual address management macros
#define PG_ADDR_DIRSHIFT           22
#define PG_ADDR_TABSHIFT           12
#define PG_ADDR_TABMASK            0x3FF000
#define PG_ADDR_DIR(addr)          ((addr) >> PG_ADDR_DIRSHIFT)
#define PG_ADDR_TAB(addr)          (((addr) & (PG_ADDR_TABMASK)) >> PG_ADDR_TABSHIFT)

// Shift table for each level for arch independent layer
static uint8_t idxShiftTab[] = {0, 12, 22};

// Macro to get level index
#define MUL_IDX_MASK               0x3FF
#define MUL_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (MUL_IDX_MASK))

// Max user directory entry
#define MUL_MAX_USER               767
#define MUL_KERNEL_START           768
#define MUL_KERNEL_MAX             1023

// Flushes TLB for cache entry
void MmMulFlushCacheEntry (uintptr_t addr);

#endif

// PT cache defines
#define MUL_MAX_PTCACHE        32
#define MUL_PTCACHE_BASE       0xBFFDF000
#define MUL_PTCACHE_TABLE_BASE 0xBFFDE000
#define MUL_PTCACHE_ENTRY_BASE 0xBFFDD000

// Obtains PTE address of specified PT cache entry
static inline pte_t* MmMulGetCacheAddr (uintptr_t addr)
{
    return (pte_t*) ((MUL_IDX_LEVEL (addr, 1) * sizeof (pte_t)) + MUL_PTCACHE_TABLE_BASE);
}

// Maps a cache entry
static inline void MmMulMapCacheEntry (pte_t* pte, paddr_t tab)
{
    *pte = tab | PF_P | PF_RW;
}

// Changes flags of entry
static inline void MmMulChangePte (pte_t* pte, int perm)
{
    *pte &= PT_FRAME;
    *pte |= perm;
}

// Validates that we can map pte2 to pte1
void MmMulVerify (pte_t pte1, pte_t pte2);

typedef struct _memspace MmSpace_t;

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent);

#endif

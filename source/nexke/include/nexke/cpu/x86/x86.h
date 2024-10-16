/*
    x86.h - contains nexke x86 stuff
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

#ifndef _X86_H
#define _X86_H

#include <stdbool.h>
#include <stdint.h>

#define MM_PAGE_TABLES

#define CPU_VENDOR_INTEL   0
#define CPU_VENDOR_AMD     1
#define CPU_VENDOR_UNKNOWN 0xFF

// CPU features
#define CPU_FEATURE_FPU           (1ULL << 0)
#define CPU_FEATURE_VME           (1ULL << 1)
#define CPU_FEATURE_DE            (1ULL << 2)
#define CPU_FEATURE_PSE           (1ULL << 3)
#define CPU_FEATURE_TSC           (1ULL << 4)
#define CPU_FEATURE_MSR           (1ULL << 5)
#define CPU_FEATURE_PAE           (1ULL << 6)
#define CPU_FEATURE_MCE           (1ULL << 7)
#define CPU_FEATURE_CMPXCHG8B     (1ULL << 8)
#define CPU_FEATURE_APIC          (1ULL << 9)
#define CPU_FEATURE_SYSENTER      (1ULL << 10)
#define CPU_FEATURE_MTRR          (1ULL << 11)
#define CPU_FEATURE_PGE           (1ULL << 12)
#define CPU_FEATURE_MCA           (1ULL << 13)
#define CPU_FEATURE_CMOV          (1ULL << 14)
#define CPU_FEATURE_PAT           (1ULL << 15)
#define CPU_FEATURE_PSE36         (1ULL << 16)
#define CPU_FEATURE_CLFLUSH       (1ULL << 17)
#define CPU_FEATURE_MMX           (1ULL << 18)
#define CPU_FEATURE_FXSR          (1ULL << 19)
#define CPU_FEATURE_SSE           (1ULL << 20)
#define CPU_FEATURE_SSE2          (1ULL << 21)
#define CPU_FEATURE_HT            (1ULL << 22)
#define CPU_FEATURE_SSE3          (1ULL << 23)
#define CPU_FEATURE_MONITOR       (1ULL << 24)
#define CPU_FEATURE_SSSE3         (1ULL << 25)
#define CPU_FEATURE_CMPXCHG16B    (1ULL << 26)
#define CPU_FEATURE_SSE41         (1ULL << 27)
#define CPU_FEATURE_POPCNT        (1ULL << 28)
#define CPU_FEATURE_LAHF          (1ULL << 29)
#define CPU_FEATURE_SYSCALL       (1ULL << 30)
#define CPU_FEATURE_XD            (1ULL << 31)
#define CPU_FEATURE_1GB           (1ULL << 32)
#define CPU_FEATURE_RDTSCP        (1ULL << 33)
#define CPU_FEATURE_LM            (1ULL << 34)
#define CPU_FEATURE_FSGSBASE      (1ULL << 35)
#define CPU_FEATURE_SMEP          (1ULL << 36)
#define CPU_FEATURE_INVPCID       (1ULL << 37)
#define CPU_FEATURE_VMX           (1ULL << 38)
#define CPU_FEATURE_PCID          (1ULL << 39)
#define CPU_FEATURE_SSE42         (1ULL << 40)
#define CPU_FEATURE_X2APIC        (1ULL << 41)
#define CPU_FEATURE_TSC_DEADLINE  (1ULL << 42)
#define CPU_FEATURE_XSAVE         (1ULL << 43)
#define CPU_FEATURE_OSXSAVE       (1ULL << 44)
#define CPU_FEATURE_AVX           (1ULL << 45)
#define CPU_FEATURE_RDRAND        (1ULL << 46)
#define CPU_FEATURE_SVM           (1ULL << 49)
#define CPU_FEATURE_SSE4A         (1ULL << 50)
#define CPU_FEATURE_SSE5          (1ULL << 51)
#define CPU_FEATURE_INVLPG        (1ULL << 52)
#define CPU_FEATURE_AC            (1ULL << 53)
#define CPU_FEATURE_ARAT          (1ULL << 54)
#define CPU_FEATURE_INVARIANT_TSC (1ULL << 55)

// Gets feature bits
uint64_t CpuGetFeatures();

// Waits for IO completion
void CpuIoWait();

// Writes byte to I/O port
void CpuOutb (uint16_t port, uint8_t val);

// Writes word to I/O port
void CpuOutw (uint16_t port, uint16_t val);

// Writes dword to I/O port
void CpuOutl (uint16_t port, uint32_t val);

// Reads byte from I/O port
uint8_t CpuInb (uint16_t port);

// Reads word from I/O port
uint16_t CpuInw (uint16_t port);

// Reads dword from I/O port
uint32_t CpuInl (uint16_t port);

// Reads specified MSR
uint64_t CpuRdmsr (uint32_t msr);

// Writes specified MSR
void CpuWrmsr (uint32_t msr, uint64_t val);

// Reads TSC
uint64_t CpuRdtsc (void);

// Invalidates TLB for page
void CpuInvlpg (uintptr_t addr);

// Crashes the CPU
void __attribute__ ((noreturn)) CpuCrash();

// CPU page size
#define NEXKE_CPU_PAGESZ     0x1000
#define NEXKE_CPU_PAGE_SHIFT 12

// Kernel stack size
#define CPU_KSTACK_SZ 8192

// Data structures

// Segment descriptor
typedef struct _x86seg
{
    uint16_t limitLow;    // Low 16 of limit
    uint16_t baseLow;     // Low 16 of base
    uint8_t baseMid;      // Mid 8 of base
    uint16_t flags;       // Segment flags plus high 4 of base
    uint8_t baseHigh;
} __attribute__ ((packed)) CpuSegDesc_t;

// Segment flags
#define CPU_SEG_NON_SYS (1 << 4)
#define CPU_SEG_PRESENT (1 << 7)
#define CPU_SEG_LONG    (1 << 13)
#define CPU_SEG_DB      (1 << 14)
#define CPU_SEG_GRAN    (1 << 15)

// Segment access
#define CPU_SEG_ACCESSED (1 << 0)
#define CPU_SEG_CODE     (1 << 3)
// Data only
#define CPU_SEG_WRITABLE    (1 << 1)
#define CPU_SEG_EXPAND_DOWN (1 << 2)
// Code only
#define CPU_SEG_READABLE   (1 << 1)
#define CPU_SEG_CONFORMING (1 << 2)

// System segment types
#define CPU_SEG_LDT       2
#define CPU_SEG_TASK_GATE 5
#define CPU_SEG_TSS       9
#define CPU_SEG_TSS_BUSY  11
#define CPU_SEG_CALL_GATE 12
#define CPU_SEG_INT_GATE  14
#define CPU_SEG_TRAP_GATE 15

// Limit high shift
#define CPU_SEG_LIMIT_SHIFT 8

// Privilge values
#define CPU_DPL_KERNEL    0
#define CPU_DPL_USER      3
#define CPU_SEG_DPL_SHIFT 5

// Segment register format
#define CPU_SEL_LDT    (1 << 2)
#define CPU_SEL_KERNEL 0
#define CPU_SEL_USER   3

// Standard segments
#define CPU_SEG_KCODE 0x8
#define CPU_SEG_KDATA 0x10
#define CPU_SEG_UCODE 0x18
#define CPU_SEG_UDATA 0x20

// GDT constants
#define CPU_GDT_MAX 8192

// Allocates a segment for a data structure. Returns segment number
int CpuAllocSeg (uintptr_t base, uintptr_t limit, int dpl);

// Frees a segment
void CpuFreeSeg (int segNum);

// Generic x86 IDT stuff
#define CPU_IDT_MAX 256
#define NK_MAX_INTS 256

// System call interrupt
#define CPU_SYSCALL_INT 0x20

// Table pointer
typedef struct _x86TabPtr
{
    uint16_t limit;
    uintptr_t base;    // Base of table
} __attribute__ ((packed)) CpuTabPtr_t;

// Installs IDT
void CpuInstallIdt (CpuTabPtr_t* idt);

// Flushes GDT
void CpuFlushGdt (CpuTabPtr_t* gdt);

// CCB type
typedef struct _nkccb NkCcb_t;

typedef struct _nkarchccb
{
    int vendor;      // CPU vendor
    int stepping;    // CPU specifier
    int model;
    int family;
    int physAddrBits;      // Number of bits in a physical address
    int virtAddrBits;      // Number of bits in virtual address
    bool intsHeld;         // If interrupts are being held
    bool intRequested;     // If unhold should enable interrupts
    uint64_t features;     // CPU feature flags
    CpuSegDesc_t* gdt;     // GDT pointer
    CpuIdtEntry_t* idt;    // IDT pointer
} NkArchCcb_t;

// Fills CCB with CPUID flags
void CpuDetectCpuid (NkCcb_t* ccb);

// CPU trap table
extern uint8_t CpuTrapTable[];

// Gets trap from vector
#define CPU_GETTRAP(vector) ((uintptr_t) (CpuTrapTable + (vector * 0x10)))

// Base hardware interrupt number
#define CPU_BASE_HWINT 48

// CPU-specific thread structure
typedef struct _cputhread
{
    // Nothing for now
} CpuThread_t;

// Segment reg helpers
#define CpuReadGs(val) asm volatile ("movl %%gs:0,%0" : "=r"((val)) :);

// Gets the current CCB
static inline NkCcb_t* CpuGetCcb()
{
    uintptr_t ccb = 0;
    CpuReadGs (ccb);
    return (NkCcb_t*) ccb;
}

// Control register bits
#define CPU_CR0_PE (1 << 0)
#define CPU_CR0_WP (1 << 16)
#define CPU_CR0_AM (1 << 18)
#define CPU_CR0_PG (1 << 31)

#define CPU_CR4_PSE        (1 << 4)
#define CPU_CR4_PAE        (1 << 5)
#define CPU_CR4_MCE        (1 << 6)
#define CPU_CR4_PGE        (1 << 7)
#define CPU_CR4_OSFXSR     (1 << 9)
#define CPU_CR4_OSXMMEXCPT (1 << 10)
#define CPU_CR4_UMIP       (1 << 11)
#define CPU_CR4_OSXSAVE    (1 << 18)
#define CPU_CR4_SMEP       (1 << 20)
#define CPU_CR4_SMAP       (1 << 21)

#define CPU_EFER_SCE (1 << 0)
#define CPU_EFER_NXE (1 << 11)
#define CPU_EFER_MSR 0xC0000080

// IDT type codes and flags
#define CPU_IDT_INT       0xF
#define CPU_IDT_TRAP      0xE
#define CPU_IDT_TASK      5
#define CPU_IDT_PRESENT   (1 << 7)
#define CPU_IDT_DPL_SHIFT 5

// Exception numbers
#define CPU_EXEC_DE  0
#define CPU_EXEC_DB  1
#define CPU_EXEC_NMI 2
#define CPU_EXEC_BP  3
#define CPU_EXEC_OF  4
#define CPU_EXEC_BR  5
#define CPU_EXEC_UD  6
#define CPU_EXEC_NM  7
#define CPU_EXEC_DF  8
#define CPU_EXEC_CPO 9
#define CPU_EXEC_TS  10
#define CPU_EXEC_NP  11
#define CPU_EXEC_SS  12
#define CPU_EXEC_GP  13
#define CPU_EXEC_PF  14
#define CPU_EXEC_MF  16
#define CPU_EXEC_AC  17
#define CPU_EXEC_MC  18
#define CPU_EXEC_XM  19
#define CPU_EXEC_VE  20
#define CPU_EXEC_CP  21
#define CPU_EXEC_MAX 31

#endif

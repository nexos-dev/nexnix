/*
    apic.c - contains APIC driver
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

#include "pc.h"
#include <assert.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>

// For disabing to 8259A
#define PLT_PIC_MASTER_DATA 0x21
#define PLT_PIC_SLAVE_DATA  0xA1

// APIC registers
#define PLT_LAPIC_ID            0x20
#define PLT_LAPIC_VERSION       0x30
#define PLT_LAPIC_TPR           0x80
#define PLT_LAPIC_APR           0x90
#define PLT_LAPIC_PPR           0xA0
#define PLT_LAPIC_EOI           0xB0
#define PLT_LAPIC_RRD           0xC0
#define PLT_LAPIC_LDR           0xD0
#define PLT_LAPIC_DFR           0xE0
#define PLT_LAPIC_SVR           0xF0
#define PLT_LAPIC_ISR_BASE      0x100
#define PLT_LAPIC_TMR_BASE      0x180
#define PLT_LAPIC_IRR_BASE      0x200
#define PLT_LAPIC_ESR           0x280
#define PLT_LAPIC_ICR1          0x300
#define PLT_LAPIC_ICR2          0x310
#define PLT_LVT_TIMER           0x320
#define PLT_LVT_THERMAL         0x330
#define PLT_LVT_PMC             0x340
#define PLT_LVT_LINT0           0x350
#define PLT_LVT_LINT1           0x360
#define PLT_LVT_ERROR           0x370
#define PLT_TIMER_INITIAL_COUNT 0x380
#define PLT_TIMER_CURRENT_COUNT 0x390
#define PLT_TIMER_DIVIDE        0x3E0

// APIC MSR defines
#define PLT_APIC_MSR_BASE   0x800
#define PLT_APIC_X2_SHIFT   4
#define PLT_APIC_BASE       0xFEE00000
#define PLT_APIC_BASE_MSR   0x1B
#define PLT_APIC_MSR_ENABLE (1 << 11)
#define PLT_APIC_MSR_X2     (1 << 10)

// LVT bits
#define PLT_APIC_PENDING        (1 << 12)
#define PLT_APIC_ACTIVE_LOW     (1 << 13)
#define PLT_APIC_REMOTE_IRR     (1 << 14)
#define PLT_APIC_LEVEL          (1 << 15)
#define PLT_APIC_MASKED         (1 << 16)
#define PLT_APIC_FIXED          (0 << 8)
#define PLT_APIC_SMI            (2 << 8)
#define PLT_APIC_NMI            (4 << 8)
#define PLT_APIC_EXT_INT        (7 << 8)
#define PLT_APIC_TIMER_ONE_SHOT (0 << 17)
#define PLT_APIC_TIMER_PERIODIC (1 << 17)
#define PLT_APIC_TIMER_TSC      (2 << 17)

// Error bits
#define PLT_APIC_ERR_SEND_CHECKSUM (1 << 0)
#define PLT_APIC_ERR_RECV_CHECKSUM (1 << 1)
#define PLT_APIC_ERR_SEND_ACCEPT   (1 << 2)
#define PLT_APIC_ERR_RECV_ACCEPT   (1 << 3)
#define PLT_APIC_ERR_REDIR_IPI     (1 << 4)
#define PLT_APIC_ERR_SEND_VECTOR   (1 << 5)
#define PLT_APIC_ERR_RECV_VECTOR   (1 << 6)
#define PLT_APIC_ERR_ILL_ADDR      (1 << 7)

// ICR bits
#define PLT_APIC_INIT_IPI           (5 << 8)
#define PLT_APIC_STARTUP_IPI        (6 << 8)
#define PLT_APIC_DEST_PHYS          (0 << 11)
#define PLT_APIC_DEST_LOGICAL       (1 << 11)
#define PLT_APIC_IPI_STATUS_PENDING (1 << 12)
#define PLT_APIC_IPI_ASSERT         (1 << 14)
#define PLT_APIC_IPI_EDGE           (0 << 15)
#define PLT_APIC_IPI_LEVEL          (1 << 15)
#define PLT_APIC_SH_SELF            (1 << 18)
#define PLT_APIC_SH_ALL             (2 << 18)
#define PLT_APIC_SH_ALL_EXEC        (3 << 18)

// SVR format
#define PLT_APIC_SVR_ENABLE  (1 << 8)
#define PLT_APIC_SUPRESS_EOI (1 << 12)

// ID register
#define PLT_APIC_ID_SHIFT 24

// I/O APIC stuff

// Memory space
#define PLT_IOAPIC_REG 0
#define PLT_IOAPIC_WIN 0x10

// Registers
#define PLT_IOAPIC_ID         0
#define PLT_IOAPIC_VER        1
#define PLT_IOAPIC_ARB        2
#define PLT_IOAPIC_BASE_REDIR 16

// Redirection entry structure
#define PLT_IOAPIC_DELIV_PENDING (1 << 12)
#define PLT_IOAPIC_ACTIVE_LOW    (1 << 13)
#define PLT_IOAPIC_ACTIVE_HIGH   (0 << 13)
#define PLT_IOAPIC_IRR           (1 << 14)
#define PLT_IOAPIC_LEVEL         (1 << 15)
#define PLT_IOAPIC_EDGE          (0 << 15)
#define PLT_IOAPIC_MASK          (1 << 16)

// Array of all IO APICs
#define PLT_IOAPIC_MAX 128

typedef struct _ioapic
{
    int id;                     // ID of APIC
    int numRedir;               // Num redirection entry of this APIC
    uint32_t gsiBase;           // GSI base of this APIC
    volatile uint32_t* addr;    // Base address of APIC
} pltIoApic_t;

static pltIoApic_t ioApics[PLT_IOAPIC_MAX + 1] = {0};

// Vectors of reserved interrupts
#define PLT_APIC_SPURIOUS    (CPU_BASE_HWINT + 8)    // Some CPUs need spurious vector bits 3:0 to be 0
#define PLT_APIC_ERROR       (CPU_BASE_HWINT + 1)
#define PLT_APIC_TIMER       (CPU_BASE_HWINT + 2)
#define PLT_APIC_BASE_VECTOR (CPU_BASE_HWINT + 9)

// Base address of APIC
static volatile void* apicBase = NULL;

// Maps IPL to APIC priority
static inline uint8_t pltLapicMapIpl (ipl_t ipl)
{
    return (uint8_t) ipl - 1;
}

// Reads lapic register
static uint32_t pltLapicRead (uint16_t regIdx)
{
    // Should be volatile so reads are cached by compiler
    volatile uint32_t* reg = (volatile uint32_t*) (apicBase + regIdx);
    return *reg;
}

// Writes lapic register
static void pltLapicWrite (uint16_t regIdx, uint32_t value)
{
    volatile uint32_t* reg = (volatile uint32_t*) (apicBase + regIdx);
    *reg = value;
}

// Reads I/O APIC register
static uint32_t pltIoApicRead (pltIoApic_t* apic, uint8_t reg)
{
    *(apic->addr) = reg;    // Set register select
    return *(apic->addr + 4);
}

// Writes I/O APIC register
static void pltIoApicWrite (pltIoApic_t* apic, uint8_t reg, uint32_t val)
{
    *(apic->addr) = reg;
    *(apic->addr + 4) = val;
}

// Writes redirection entry
static void pltIoApicWriteRedir (pltIoApic_t* apic, uint8_t vector, uint64_t entry)
{
    pltIoApicWrite (apic, PLT_IOAPIC_BASE_REDIR + (vector * 2), entry & 0xFFFFFFFF);
    pltIoApicWrite (apic, PLT_IOAPIC_BASE_REDIR + (vector * 2) + 1, entry >> 32ULL);
}

// Gets I/O APIC associated with GSI
static pltIoApic_t* pltApicGetIoApic (uint32_t gsi)
{
    pltIoApic_t* cur = ioApics;
    while (cur->addr)
    {
        if (gsi > cur->gsiBase)
        {
            // Check if we are less than next entry's base, or if this is the last one
            if (!(cur + 1)->addr || (cur + 1)->gsiBase > gsi)
            {
                // Ensure we are inside max redirection entry
                if (gsi - cur->gsiBase > cur->numRedir)
                    return NULL;    // Doesn't exist
                return cur;
            }
        }
        ++cur;
    }
    return NULL;
}

// Interface functions
static bool PltApicBeginInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // If not fake convert line to vector
    int line = intObj->line;
    if (!(intObj->flags & PLT_HWINT_FAKE))
        line += CPU_BASE_HWINT;
    // Check if this is spurious, error, or timer
    if (line == PLT_APIC_SPURIOUS)
    {
        // If fake let caller know we are spurious
        if (intObj->flags & PLT_HWINT_FAKE)
            intObj->flags |= PLT_HWINT_SPURIOUS;
        return false;
    }
    else if (intObj->line == PLT_APIC_ERROR)
    {
        // An APIC error occured, panic
        // Or should we not panic?
        NkPanic ("nexke: internal APIC error\n");
    }
    else if (intObj->line == PLT_APIC_TIMER)
    {
        // TODO
    }
    return true;
}

static void PltApicEndInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    pltLapicWrite (PLT_LAPIC_EOI, 0);    // Send EOI
}

static void PltApicDisableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // TODO
}

static void PltApicEnableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // TODO
}

static void PltApicSetIpl (NkCcb_t* ccb, ipl_t ipl)
{
    // Convert IPL to APIC priority
    uint8_t priority = pltLapicMapIpl (ipl);
    // Set the TPR
    pltLapicWrite (PLT_LAPIC_TPR, priority);
}

static int PltApicConnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
}

static void PltApicDisconnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
}

PltHwIntCtrl_t pltApic = {.type = PLT_HWINT_APIC,
                          .beginInterrupt = PltApicBeginInterrupt,
                          .connectInterrupt = PltApicConnectInterrupt,
                          .disableInterrupt = PltApicDisableInterrupt,
                          .disconnectInterrupt = PltApicDisconnectInterrupt,
                          .enableInterrupt = PltApicEnableInterrupt,
                          .endInterrupt = PltApicEndInterrupt};

static bool pltLapicInit()
{
    // Check if APIC exists
    NkCcb_t* ccb = CpuGetCcb();
    if (!(ccb->archCcb.features & CPU_FEATURE_APIC))
        return false;    // APIC doesn't exist
    // Get APIC base
    apicBase = MmAllocKvMmio ((void*) PLT_APIC_BASE,
                              1,
                              MUL_PAGE_CD | MUL_PAGE_RW | MUL_PAGE_R | MUL_PAGE_KE);
    // Enable it in the MSR
    uint64_t apicBase = CpuRdmsr (PLT_APIC_BASE_MSR);
    apicBase |= PLT_APIC_MSR_ENABLE;
    CpuWrmsr (PLT_APIC_BASE_MSR, apicBase);
    // Disable 8295A PIC
    CpuOutb (PLT_PIC_MASTER_DATA, 0xFF);
    CpuOutb (PLT_PIC_SLAVE_DATA, 0xFF);
    // Get number LVT entries
    uint32_t maxLvt = (pltLapicRead (PLT_LAPIC_VERSION) >> 16) & 0xFF;
    // Setup SVR to enable APIC
    pltLapicWrite (PLT_LAPIC_SVR, PLT_APIC_SPURIOUS | PLT_APIC_SVR_ENABLE);
    // Setup error LVT entry
    pltLapicWrite (PLT_LVT_ERROR, PLT_APIC_ERROR);
    // Mask all other LVT entries
    pltLapicWrite (PLT_LVT_LINT0, PLT_APIC_MASKED);
    pltLapicWrite (PLT_LVT_LINT1, PLT_APIC_MASKED);
    // Check if we have a thermal and performance vector
    if (maxLvt >= 4)
        pltLapicWrite (PLT_LVT_PMC, PLT_APIC_MASKED);
    if (maxLvt >= 5)
        pltLapicWrite (PLT_LVT_THERMAL, PLT_APIC_MASKED);
    // Mask timer for now
    pltLapicWrite (PLT_LVT_TIMER, PLT_APIC_MASKED);
    // Clear any potentially pending interrupts
    pltLapicWrite (PLT_LAPIC_EOI, 0);
    // Set TPR
    pltLapicWrite (PLT_LAPIC_TPR, 0);
    // Find platform CPU with this APIC ID so we can determine the BSP
    int selfId = pltLapicRead (PLT_LAPIC_ID) >> 24;
    PltCpu_t* curCpu = PltGetPlatform()->cpus;
    while (curCpu)
    {
        if (curCpu->id == selfId)
        {
            // Set this as BSP
            PltGetPlatform()->bsp = curCpu;
        }
        curCpu = curCpu->next;
    }
    assert (PltGetPlatform()->bsp);
    return true;
}

PltHwIntCtrl_t* PltApicInit()
{
    // Initialize LAPIC first
    if (!pltLapicInit())
        return NULL;
    NkLogDebug ("nexke: using APIC as interrupt controller\n");
    // Initialize I/O APICs
    PltIntCtrl_t* cur = PltGetPlatform()->intCtrls;
    int i = 0;
    while (cur)
    {
        if (cur->type != PLT_INTCTRL_IOAPIC)
        {
            // Skip
            cur = cur->next;
            continue;
        }
        // Get internal structure
        pltIoApic_t* ioapic = &ioApics[i];
        // Check if we need to allocate to pages
        size_t numPages = 1;
        if (((cur->addr + 0x20) / NEXKE_CPU_PAGESZ) > (cur->addr / NEXKE_CPU_PAGESZ))
            numPages = 2;
        ioapic->addr = MmAllocKvMmio ((void*) cur->addr,
                                      numPages,
                                      MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_CD);
        ioapic->gsiBase = cur->gsiBase;
        ioapic->id = pltIoApicRead (ioapic, PLT_IOAPIC_ID) >> 24;
        // Get number of redirection entries and mask them
        uint8_t numRedir = ((pltIoApicRead (ioapic, PLT_IOAPIC_VER) >> 16) & 0xFF) + 1;
        for (int i = 0; i < numRedir; ++i)
        {
            pltIoApicWriteRedir (ioapic, i, PLT_IOAPIC_MASK);    // Mask it
        }
        ioapic->numRedir = numRedir;
        ++i;
        cur = cur->next;
    }
    return &pltApic;
}

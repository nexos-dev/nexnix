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

#include <assert.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/pc.h>
#include <stdlib.h>

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

// APIC divisors
#define PLT_APIC_DIV_2   0
#define PLT_APIC_DIV_4   1
#define PLT_APIC_DIV_8   2
#define PLT_APIC_DIV_16  3
#define PLT_APIC_DIV_32  8
#define PLT_APIC_DIV_64  9
#define PLT_APIC_DIV_128 10
#define PLT_APIC_DIV_1   11

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
#define PLT_IOAPIC_DEST_SHIFT    56ULL

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

#define PLT_APIC_MAX_IPL             25
#define PLT_APIC_CLASS_TO_PRI(class) ((class) << 4)
#define PLT_APIC_PRI_TO_CLASS(pri)   ((pri) >> 4)

// Vector allocation map
typedef struct _priority
{
    bool vectors[16];    // Vector allocation map
} pltApicPriority_t;

// Vector map
#define PLT_APIC_NUM_PRIORITY 16
static pltApicPriority_t vectorMap[PLT_APIC_NUM_PRIORITY] = {0};

static uint8_t prioToIplMap[] = {0, 0, 0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24};

// Vectors of reserved interrupts
#define PLT_APIC_SPURIOUS       243
#define PLT_APIC_ERROR          241
#define PLT_APIC_TIMER          242
#define PLT_APIC_BASE_VECTOR    (CPU_BASE_HWINT)
#define PLT_APIC_LAST_USER_PRIO 15

// Base address of APIC
static volatile void* apicBase = NULL;

// APIC timer state
static bool isApicTimer = false;
static int armCount = 0;
static int finalArm = 0;
extern PltHwTimer_t pltApicTimer;

// Maps IPL to APIC priority
static inline uint8_t pltLapicMapIpl (ipl_t ipl)
{
    // Bounds check
    if (ipl == PLT_IPL_TIMER)
    {
        // Ensure we have max prio
        return PLT_APIC_CLASS_TO_PRI (PLT_APIC_NUM_PRIORITY - 1);
    }
    if (ipl > PLT_APIC_MAX_IPL)
        ipl = PLT_APIC_MAX_IPL;
    return (uint8_t) PLT_APIC_CLASS_TO_PRI ((ipl / 2) + 3);
}

// Maps priority to IPL
static inline ipl_t pltLapicMapPrio (uint8_t class)
{
    return prioToIplMap[class];
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

// Reads redirection entry
static uint64_t pltIoApicReadRedir (pltIoApic_t* apic, uint8_t vector)
{
    uint64_t redir = pltIoApicRead (apic, PLT_IOAPIC_BASE_REDIR + (vector * 2));
    redir |= (uint64_t) (pltIoApicRead (apic, PLT_IOAPIC_BASE_REDIR + (vector * 2) + 1)) << 32ULL;
    return redir;
}

// Gets I/O APIC associated with GSI
static pltIoApic_t* pltApicGetIoApic (uint32_t gsi)
{
    pltIoApic_t* cur = ioApics;
    while (cur->addr)
    {
        if (gsi >= cur->gsiBase)
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

// Handles APIC timer event
static inline void pltLapicTimer()
{
    if (armCount)
    {
        --armCount;    // Decrease pending arm counter
        if (!armCount)
        {
            // Set final arm
            pltLapicWrite (PLT_TIMER_INITIAL_COUNT, finalArm);
        }
        else
            pltLapicWrite (PLT_TIMER_INITIAL_COUNT, 0xFFFFFFFF);
    }
    else
    {
        // Call the callback to drain the event queue
        pltApicTimer.callback();
    }
}

// Interface functions
static bool PltApicBeginInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // If not fake convert line to vector
    uint32_t gsi = intObj->gsi;
    if (!(intObj->flags & PLT_HWINT_FAKE))
        gsi += CPU_BASE_HWINT;
    // Check if this is spurious, error, or timer
    if (gsi == PLT_APIC_SPURIOUS)
    {
        // If fake let caller know we are spurious
        if (intObj->flags & PLT_HWINT_FAKE)
            intObj->flags |= PLT_HWINT_SPURIOUS;
        return false;
    }
    else if (gsi == PLT_APIC_ERROR)
    {
        NkLogWarning ("nexke: warning: APIC error\n");
        pltLapicWrite (PLT_LAPIC_EOI, 0);
        return false;
    }
    else if (gsi == PLT_APIC_TIMER)
    {
        // A timer interrupt occured, call the handler
        pltLapicTimer();
        CpuDisable();
        pltLapicWrite (PLT_LAPIC_EOI, 0);
        return false;    // We handled it
    }
    return true;
}

static void PltApicEndInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    CpuDisable();
    pltLapicWrite (PLT_LAPIC_EOI, 0);    // Send EOI
}

static void PltApicDisableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    pltIoApic_t* apic = pltApicGetIoApic (intObj->gsi);
    assert (apic);
    uint32_t line = intObj->gsi - apic->gsiBase;
    pltIoApicWriteRedir (apic, line, pltIoApicReadRedir (apic, line) | PLT_IOAPIC_MASK);
}

static void PltApicEnableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    pltIoApic_t* apic = pltApicGetIoApic (intObj->gsi);
    assert (apic);
    uint32_t line = intObj->gsi - apic->gsiBase;
    pltIoApicWriteRedir (apic, line, pltIoApicReadRedir (apic, line) & ~(PLT_IOAPIC_MASK));
}

static void PltApicSetIpl (NkCcb_t* ccb, ipl_t ipl)
{
    // Special case for timer
    if (ipl == PLT_IPL_TIMER)
    {
        // Set the TPR
        pltLapicWrite (PLT_LAPIC_TPR, 0xE0);
    }
    else if (!ipl)
        pltLapicWrite (PLT_LAPIC_TPR, 0);
    else
    {
        // Convert IPL to APIC priority
        uint8_t priority = pltLapicMapIpl (ipl - 1);
        // Set the TPR
        pltLapicWrite (PLT_LAPIC_TPR, priority);
    }
}

static int PltApicConnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // First allocate a vector for this interrupt
    uint8_t priority = pltLapicMapIpl (intObj->ipl);
    assert (priority >= PLT_APIC_CLASS_TO_PRI (2));
    // Get allocation map entry
    pltApicPriority_t* baseMapEnt = &vectorMap[PLT_APIC_PRI_TO_CLASS (priority)];
    pltApicPriority_t* curMapEnt = baseMapEnt;
    int vector = 0;
    int baseClass = PLT_APIC_PRI_TO_CLASS (priority);
    int curClass = baseClass;
    int curDist = 0;
    bool isUp = false;
    bool downDone = false;
    bool upDone = false;
    while (!vector)
    {
        // Find a free vector in map entry
        for (int i = 0; i < 16; ++i)
        {
            if (!curMapEnt->vectors[i])
            {
                vector = (curClass << 4) + i;    // We have the vector
                break;
            }
        }
        if (!vector)
        {
            // Check if we're done
            if (downDone && upDone)
                break;
            // Move to next priority
            // The way we do this is by alternating up and down, i.e., down one,
            // up one, down two, up two, down three, up three, etc
            if (!isUp)
            {
                ++curDist;       // Move to next distance
                if (curClass)    // Don't go too far
                {
                    // Move down from base to curDist
                    curMapEnt = baseMapEnt - curDist;
                    curClass = baseClass - curDist;
                }
                else
                    downDone = true;
            }
            else
            {
                // Move up
                if (curClass < PLT_APIC_LAST_USER_PRIO)
                {
                    curMapEnt = baseMapEnt + curDist;
                    curClass = baseClass + curDist;
                }
                else
                    upDone = true;
            }
        }
    }
    if (!vector)
    {
        // No free vectors
        // TODO: vector / line sharing
        return -1;
    }
    // We found a vector, make sure we set the right IPL
    // Exception for PLT_IPL_TIMER
    if (intObj->ipl == PLT_IPL_TIMER)
    {
        // Make sure we got max class
        if (curClass != PLT_APIC_NUM_PRIORITY - 1)
            return -1;    // Not valid
    }
    else
        intObj->ipl = pltLapicMapPrio (curClass);
    // Get IO APIC
    pltIoApic_t* apic = pltApicGetIoApic (intObj->gsi);
    if (!apic)
        return false;    // Invalid line
    // We are error safe, reserve the vector
    curMapEnt->vectors[vector - (curClass << 4)] = true;
    intObj->vector = vector;
    // Setup the redirection entry
    uint64_t redir = PLT_APIC_FIXED | PLT_IOAPIC_MASK;
    if (intObj->flags & PLT_HWINT_ACTIVE_LOW)
        redir |= PLT_IOAPIC_ACTIVE_LOW;
    else
        redir |= PLT_IOAPIC_ACTIVE_HIGH;
    if (intObj->mode == PLT_MODE_EDGE)
        redir |= PLT_IOAPIC_EDGE;
    else
        redir |= PLT_IOAPIC_LEVEL;
    redir |= vector & 0xFF;
    redir |= (uint64_t) (PltGetPlatform()->bsp->id) << PLT_IOAPIC_DEST_SHIFT;
    // Now write it out
    pltIoApicWriteRedir (apic, intObj->gsi - apic->gsiBase, redir);
    return vector;
}

static void PltApicDisconnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // Grab the vector and free it
    int class = PLT_APIC_PRI_TO_CLASS (intObj->vector);
    pltApicPriority_t* mapEntry = &vectorMap[class];
    // Free it
    mapEntry->vectors[intObj->vector - PLT_APIC_CLASS_TO_PRI (class)] = false;
    // Unmap interrupt from vector
    pltIoApic_t* apic = pltApicGetIoApic (intObj->gsi);
    assert (apic);
    // Disconnect it
    pltIoApicWriteRedir (apic, intObj->gsi - apic->gsiBase, PLT_IOAPIC_MASK);
}

// Helper function to get number of redirection entries for specified IOAPIC
int PltApicGetRedirs (paddr_t base)
{
    // Read version register
    volatile uint32_t* apic =
        (volatile uint32_t*) MmAllocKvMmio (base,
                                            2,
                                            MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_CD);
    assert (apic);
    *(apic) = PLT_IOAPIC_VER;
    uint32_t ver = *(apic + 4);
    MmFreeKvMmio ((void*) apic);
    return ((ver >> 16) & 0xFF) + 1;
}

PltHwIntCtrl_t pltApic = {.type = PLT_HWINT_APIC,
                          .beginInterrupt = PltApicBeginInterrupt,
                          .connectInterrupt = PltApicConnectInterrupt,
                          .disableInterrupt = PltApicDisableInterrupt,
                          .disconnectInterrupt = PltApicDisconnectInterrupt,
                          .enableInterrupt = PltApicEnableInterrupt,
                          .endInterrupt = PltApicEndInterrupt,
                          .setIpl = PltApicSetIpl};

static void pltApicSetCallback (void (*cb)())
{
    pltApicTimer.callback = cb;
}

static void pltApicArmTimer (uint64_t delta)
{
    if (armCount)
        armCount = 0, finalArm = 0;
    delta /= pltApicTimer.precision;    // Get to timer precision
    // Make sure delta is not 0
    if (!delta)
        ++delta;
    // Make sure we are in the max interval
    uint64_t maxInterval = pltApicTimer.maxInterval / pltApicTimer.precision;
    if (delta > maxInterval)
    {
        // Figure out the number of arms we will need to do
        armCount = delta / maxInterval;
        finalArm = delta % maxInterval;
        // Set delta to max
        delta = maxInterval;
    }
    // Set it in the APIC
    pltLapicWrite (PLT_TIMER_INITIAL_COUNT, (uint32_t) delta);
}

static bool pltLapicInit()
{
    // Check if APIC exists
    NkCcb_t* ccb = CpuGetCcb();
    if (!(ccb->archCcb.features & CPU_FEATURE_APIC))
        return false;    // APIC doesn't exist
    // Get APIC base
    apicBase =
        MmAllocKvMmio (PLT_APIC_BASE, 1, MUL_PAGE_CD | MUL_PAGE_RW | MUL_PAGE_R | MUL_PAGE_KE);
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
    // Clear ESR
    pltLapicWrite (PLT_LAPIC_ESR, 0);
    pltLapicWrite (PLT_LAPIC_ESR, 0);
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
            NkLogDebug ("nexke: found BSP at CPU %d\n", curCpu->id);
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
        ioapic->addr = MmAllocKvMmio (cur->addr,
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

PltHwTimer_t pltApicTimer = {.type = PLT_TIMER_APIC,
                             .setCallback = pltApicSetCallback,
                             .armTimer = pltApicArmTimer};

PltHwTimer_t* PltApicInitTimer()
{
    // Check if APIC exists
    NkCcb_t* ccb = CpuGetCcb();
    if (!(ccb->archCcb.features & CPU_FEATURE_APIC))
        return false;
    // Set up divide register
    pltLapicWrite (PLT_TIMER_DIVIDE, PLT_APIC_DIV_16);
    // Set up LVT entry, still keeping it masked
    pltLapicWrite (PLT_LVT_TIMER, PLT_APIC_TIMER | PLT_APIC_TIMER_ONE_SHOT | PLT_APIC_MASKED);
    // Set initial count
    pltLapicWrite (PLT_TIMER_INITIAL_COUNT, 0xFFFFFFFF);
    // Poll for 100ms
    PltGetPlatform()->clock->poll (PLT_NS_IN_SEC / 10);
    uint32_t ticks = 0xFFFFFFFF - pltLapicRead (PLT_TIMER_CURRENT_COUNT);
    // Convert to precision
    ticks *= 10;    // Ticks per second now
    pltApicTimer.precision = PLT_NS_IN_SEC / ticks;
    pltApicTimer.maxInterval = UINT32_MAX * pltApicTimer.precision;
    isApicTimer = true;
    // Unmask the interrupt
    pltLapicWrite (PLT_LVT_TIMER, pltLapicRead (PLT_LVT_TIMER) & ~(PLT_APIC_MASKED));
    NkLogDebug ("nexke: using APIC as timer, precision %uns\n", pltApicTimer.precision);
    return &pltApicTimer;
}

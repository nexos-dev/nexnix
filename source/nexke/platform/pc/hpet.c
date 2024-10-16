/*
    hpet.c - contains HPET timer driver
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

// HPET ACPI table
typedef struct _hpetacpi
{
    AcpiSdt_t sdt;
    uint32_t blockId;    // Copy of capabilities register
    AcpiGas_t base;      // GAS base
    uint8_t seqNum;    // Sequence number of table. We only are able to work with one HPET as of now
    uint16_t minPeriod;    // Min number of clock ticks periodic mode can be set at without lost
                           // interrupts
    uint8_t pageProt;      // Page protection from OEM
} __attribute__ ((packed)) AcpiHpet_t;

// HPET registers
#define PLT_HPET_GEN_CAP    0x0
#define PLT_HPET_GEN_CONF   0x10
#define PLT_HPET_INT_STATUS 0x20
#define PLT_HPET_COUNTER    0xF0

// Cap register defines
#define PLT_HPET_REV_MASK     0xFF
#define PLT_HPET_TIMER_SHIFT  8
#define PLT_HPET_TIMER_MASK   0xF
#define PLT_HPET_COUNT_SZ     (1 << 13)
#define PLT_HPET_LEG_ROUTE    (1 << 15)
#define PLT_HPET_PERIOD_SHIFT 32ULL

// Conf register
#define PLT_HPET_ENABLE       (1 << 0)
#define PLT_HPET_LEG_ROUTE_EN (1 << 1)

// Timer registers
#define PLT_HPET_TIMER_BASE      0x100
#define PLT_HPET_TIMER_CONF      0x0
#define PLT_HPET_TIMER_COMP      0x8
#define PLT_HPET_TIMER_FSB_ROUTE 0x10
#define PLT_HPET_TIMER_SZ        0x20

// Timer conf
#define PLT_HPET_TIMER_LEVEL     (1 << 1)
#define PLT_HPET_TIMER_INT       (1 << 2)
#define PLT_HPET_TIMER_PERIODIC  (1 << 3)
#define PLT_HPET_TIMER_PER_CAP   (1 << 4)
#define PLT_HPET_TIMER_64        (1 << 5)
#define PLT_HPET_TIEMR_SET       (1 << 6)
#define PLT_HPET_TIMER_32        (1 << 8)
#define PLT_HPET_ROUTE_SHIFT     9
#define PLT_HPET_ROUTE_MASK      0x1F
#define PLT_HPET_FSB_ENABLE      (1 << 14)
#define PLT_HPET_FSB_CAP         (1 << 15)
#define PLT_HPET_ROUTE_CAP_SHIFT 32

typedef struct _hpet
{
    uintptr_t addr;       // Address of HPET
    bool isTimer64;       // Is timer 64 bit?
    ktime_t lastRead;     // Last clock read
    int overflowCount;    // Number of overflows to occur
    uint32_t div;         // If precision is beyond nanosec, divide by this
    int armCount;         // Times to arm timer before expiration
    ktime_t finalArm;     // Final arm value
    uint32_t minDelta;    // Minimum delta
} pltHpet_t;

static pltHpet_t hpet = {0};

// Forward declarations of clock and timer
extern PltHwClock_t pltHpetClock;
extern PltHwTimer_t pltHpetTimer;

// Reads HPET register
static uint32_t pltHpetRead32 (uint16_t reg)
{
    volatile uint32_t* base = (volatile uint32_t*) (hpet.addr + reg);
    return *base;
}

static uint64_t pltHpetRead64 (uint16_t reg)
{
    volatile uint64_t* base = (volatile uint64_t*) (hpet.addr + reg);
    return *base;
}

// Writes HPET register
static void pltHpetWrite32 (uint16_t reg, uint32_t val)
{
    volatile uint32_t* base = (volatile uint32_t*) (hpet.addr + reg);
    *base = val;
}

static void pltHpetWrite64 (uint16_t reg, uint64_t val)
{
    volatile uint64_t* base = (volatile uint64_t*) (hpet.addr + reg);
    *base = val;
}

// Writes to timer register
static void pltHpetTimerWrite (int timer, uint16_t reg, uint64_t val)
{
    volatile uint64_t* base =
        (volatile uint64_t*) (hpet.addr + PLT_HPET_TIMER_BASE + (timer * PLT_HPET_TIMER_SZ) + reg);
    *base = val;
}

// Reads timer register
static uint64_t pltHpetTimerRead (int timer, uint16_t reg)
{
    volatile uint64_t* base =
        (volatile uint64_t*) (hpet.addr + PLT_HPET_TIMER_BASE + (timer * PLT_HPET_TIMER_SZ) + reg);
    return *base;
}

// Converts to HPET precision
static inline ktime_t pltFromHpetTime (ktime_t val)
{
    if (pltHpetClock.precision == 1)
        return val / hpet.div;
    return val * pltHpetClock.precision;
}

// Converts to ns precision
static inline ktime_t pltToHpetTime (ktime_t val)
{
    if (pltHpetClock.precision == 1)
        return val * hpet.div;
    return val / pltHpetClock.precision;
}

// Timer interrupt handler
static bool PltHpetDispatch (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    if (hpet.armCount)
    {
        --hpet.armCount;
        if (!hpet.armCount)
            pltHpetTimerWrite (0, PLT_HPET_TIMER_COMP, hpet.finalArm);    // Arm the final arm
        else
            pltHpetTimerWrite (0, PLT_HPET_TIMER_COMP, UINT32_MAX);    // Normal arm
    }
    else
    {
        if (pltHpetTimer.callback)
            pltHpetTimer.callback();    // Call the callback
    }
    // Clear timer 0 interrupt
    pltHpetWrite64 (PLT_HPET_INT_STATUS, (1 << 0));
    return true;
}

// Gets time of clock
static ktime_t PltHpetGetTime()
{
    ktime_t ret = 0;
    if (!hpet.isTimer64)
    {
        // Grab value. Note that we have to deal with rollover
        uint32_t val = pltHpetRead32 (PLT_HPET_COUNTER);
        if (val < hpet.lastRead)
        {
            // Timer rolled. Increment overflow counter
            ++hpet.overflowCount;
        }
        hpet.lastRead = val;
        ret = ((ktime_t) hpet.overflowCount << 32ULL) + val;
    }
    else
    {
        // If this is a 64 bit system, this is easy
        if (sizeof (uintptr_t) == 8)
            ret = pltHpetRead64 (PLT_HPET_COUNTER);
        else
        {
            // We have to atomically read the counter
            // Note that we have to worry about rollover occuring between the two reads
            uint32_t highVal = 0, lowVal = 0;
            while (1)
            {
                uint32_t highVal2 = 0;
                highVal = pltHpetRead32 (PLT_HPET_COUNTER + 4);
                lowVal = pltHpetRead32 (PLT_HPET_COUNTER);
                highVal2 = pltHpetRead32 (PLT_HPET_COUNTER + 4);
                if (highVal2 != highVal)
                    continue;    // Try again
                else
                    break;    // We are done
            }
            ret = ((ktime_t) highVal << 32ULL) | lowVal;    // Set it
        }
    }
    return pltFromHpetTime (ret);
}

// Sets timer callback
static void PltHpetSetCb (void (*cb)())
{
    pltHpetTimer.callback = cb;
}

// Arms timer
static void PltHpetArmTimer (ktime_t delta)
{
    // Disarm if needed
    if (hpet.armCount)
        hpet.armCount = 0, hpet.finalArm = 0;
    // Get comparator value
    ktime_t refTick = PltGetPlatform()->clock->getTime();
    ktime_t time = refTick + delta;
    ktime_t compVal = pltToHpetTime (time);
    // Check for overflow on 32 bit timers
    if (!hpet.isTimer64 && compVal > UINT32_MAX)
    {
        // We have three parts: the intial arm, how many max arms,
        // and the final arm
        // Compute them
        uint32_t firstArm = 0xFFFFFFFF - refTick;
        compVal -= firstArm;
        // Now get how many times we need to arm at UINT32_MAX
        hpet.armCount = compVal / (UINT32_MAX + 1);
        hpet.finalArm = compVal % (UINT32_MAX + 1);
        compVal = firstArm;
    }
    ktime_t minComp = pltToHpetTime (refTick) + hpet.minDelta;
    if (compVal < minComp)
        compVal = minComp;    // To avoid interrupt loss
    // Write it
    pltHpetTimerWrite (0, PLT_HPET_TIMER_COMP, compVal);
}

// Polls clock for specified ns
static void PltHpetPoll (ktime_t ns)
{
    ktime_t target = ns + PltHpetGetTime();
    while (1)
    {
        if (PltHpetGetTime() >= target)
            break;
    }
}

PltHwClock_t pltHpetClock = {.type = PLT_CLOCK_HPET,
                             .getTime = PltHpetGetTime,
                             .poll = PltHpetPoll};

PltHwTimer_t pltHpetTimer = {.type = PLT_TIMER_HPET,
                             .armTimer = PltHpetArmTimer,
                             .setCallback = PltHpetSetCb};

// HPET clock initialization function
PltHwClock_t* PltHpetInitClock()
{
    pltHpetClock.private = (uintptr_t) &hpet;
    // Find ACPI table
    AcpiHpet_t* hpetAcpi = (AcpiHpet_t*) PltAcpiFindTable ("HPET");
    if (!hpetAcpi)
        return NULL;    // No HPET
    // Make sure it's MMIO
    if (hpetAcpi->base.asId != ACPI_GAS_MEM)
    {
        NkLogDebug ("nexke: unable to use HPET\n");
        return NULL;    // We only support MMIO HPET
    }
    // Map it
    hpet.addr = (uintptr_t) MmAllocKvMmio ((paddr_t) hpetAcpi->base.addr,
                                           1,
                                           MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_CD);
    assert (hpet.addr);
    // Detect precision
    uint64_t genCap = pltHpetRead64 (PLT_HPET_GEN_CAP);
    uint32_t prec = genCap >> PLT_HPET_PERIOD_SHIFT;
    // precision is currently in femtoseconds, get to nanoseconds
    uint32_t precNs = prec / 1000000;
    if (!precNs)
        ++precNs;
    hpet.div = 1000000 / prec;
    if (!hpet.div)
        ++hpet.div;
    pltHpetClock.precision = precNs;
    // Check for 64-bit
    if (genCap & PLT_HPET_COUNT_SZ)
        hpet.isTimer64 = true;
    // Enable it
    pltHpetWrite64 (PLT_HPET_GEN_CONF, pltHpetRead64 (PLT_HPET_GEN_CONF) | PLT_HPET_ENABLE);
    NkLogDebug ("nexke: using HPET as clock, precision %ldns\n", pltHpetClock.precision);
    return &pltHpetClock;
}

// HPET timer intialization function
PltHwTimer_t* PltHpetInitTimer()
{
    pltHpetTimer.private = (uintptr_t) &hpet;
    if (!hpet.addr)
        return NULL;    // No HPET was found when looking for a clock
    // Set precision
    pltHpetTimer.precision = pltHpetClock.precision;
    // Figure out max interval
    pltHpetTimer.maxInterval = pltFromHpetTime ((hpet.isTimer64) ? UINT64_MAX : UINT32_MAX);
    // Set minimum delta that can occur without loss
    hpet.minDelta = 12000 / pltHpetTimer.precision;    // TODO: we need a better way to do this
    // Program timer
    uint64_t timerCnf = pltHpetTimerRead (0, PLT_HPET_TIMER_CONF);
    if (timerCnf & PLT_HPET_FSB_CAP)
        NkLogDebug ("nexke: unable to use HPET FSB interrupt\n");
    // Check if we can use legacy replacement mode
    int line = 0;
    if (pltHpetRead64 (PLT_HPET_GEN_CAP) & PLT_HPET_LEG_ROUTE)
    {
        // NOTE: ideally I would not use legacy replacement mode (as it constrains us)
        // However some devices (like QEMU) have terrible HPET implementations
        // that effectively break without legacy replacement mode
        // Enable it
        NkLogDebug ("nexke: using legacy replacement HPET mode\n");
        pltHpetWrite64 (PLT_HPET_GEN_CONF,
                        pltHpetRead64 (PLT_HPET_GEN_CONF) | PLT_HPET_LEG_ROUTE_EN);
        line = PltGetGsi (PLT_BUS_ISA, 0);    // Get the line
    }
    else
    {
        NkLogDebug ("nexke: using I/O APIC HPET mode\n");
        // Figure out interrupts we are capable of
        uint32_t ints = timerCnf >> PLT_HPET_ROUTE_CAP_SHIFT;
        // Find the first available interrupt
        int line = 0;
        for (int i = 0; i < 32; ++i)
        {
            if (ints & (1 << i))
            {
                line = i;    // Use it
                break;
            }
        }
        // Set line
        timerCnf |= (line << PLT_HPET_ROUTE_SHIFT) & PLT_HPET_ROUTE_MASK;
        pltHpetTimerWrite (0, PLT_HPET_TIMER_CONF, timerCnf);
    }
    timerCnf |= PLT_HPET_TIMER_LEVEL |
                PLT_HPET_TIMER_INT;    // We want level trigerred since they're more robust
    pltHpetTimerWrite (0, PLT_HPET_TIMER_CONF, timerCnf);
    // Install the interrupt
    NkHwInterrupt_t* intObj = PltAllocHwInterrupt();
    intObj->mode = PLT_MODE_LEVEL;
    intObj->gsi = line;
    intObj->ipl = PLT_IPL_TIMER;
    intObj->flags = 0;
    intObj->handler = PltHpetDispatch;
    int vector = PltConnectInterrupt (intObj);
    if (vector == -1)
        NkPanic ("nexke: unable to install HPET interrupt\n");
    // Install it
    PltInstallInterrupt (vector, intObj);
    NkLogDebug ("nexke: using HPET as timer, precision %ldns\n", pltHpetTimer.precision);
    return &pltHpetTimer;
}

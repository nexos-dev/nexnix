/*
    pit.c - contains driver for PIT timer
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
#include <nexke/nexke.h>
#include <nexke/platform.h>

// PIT ports
#define PLT_PIT_CHAN0    0x40
#define PLT_PIT_CHAN1    0x41
#define PLT_PIT_CHAN2    0x42
#define PLT_PIT_MODE_CMD 0x43

// PIT frequency
#define PLT_PIT_FREQUENCY 1193180
#define PLT_PIT_HZ        100

// PIT mode / cmd register bits
#define PLT_PIT_BCD        (1 << 0)
#define PLT_PIT_ONESHOT    (0)
#define PLT_PIT_HW_ONESHOT (1 << 1)
#define PLT_PIT_RATEGEN    (2 << 1)
#define PLT_PIT_SQWAVE     (3 << 1)
#define PLT_PIT_SW_STROBE  (4 << 1)
#define PLT_PIT_HW_STROBE  (5 << 1)
#define PLT_PIT_LATCH      (0)
#define PLT_PIT_LOHI       (3 << 4)
#define PLT_PIT_SEL_CHAN0  (0)
#define PLT_PIT_SEL_CHAN1  (1 << 6)
#define PLT_PIT_SEL_CHAN2  (2 << 6)
#define PLT_PIT_READBACK   (3 << 6)

extern PltHwTimer_t pitTimer;
extern PltHwClock_t pitClock;

typedef struct _pitpvt
{
    bool isPitClock;    // Is PIT being used for clock
} pltPitPrivate_t;

static pltPitPrivate_t pitPvt = {0};

// Sets callback of timer
static void PltPitSetCallback (void (*cb)())
{
    pitTimer.callback = cb;
}

// Arms timer to delta
static void PltPitArmTimer (uint64_t delta)
{
    assert (delta <= pitTimer.maxInterval);
    // Set it
    CpuOutb (PLT_PIT_CHAN0, delta & 0xFF);
    CpuOutb (PLT_PIT_CHAN0, delta >> 8);
}

// PIT clock interrupt handler
static bool PltPitDispatch (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Check if PIT is in periodic mode or not
    if (pitPvt.isPitClock)
        pitClock.internalCount += pitClock.precision;    // Increase count
    // Call the callback. In periodic mode software must check for deadlines on every tick
    // In one shot this will drain the current deadline
    if (pitTimer.callback)
        pitTimer.callback();
    return true;
}

// Gets current PIT time
static uint64_t PltPitGetTime()
{
    return pitClock.internalCount;
}

PltHwTimer_t pitTimer = {.type = PLT_TIMER_PIT,
                         .armTimer = PltPitArmTimer,
                         .setCallback = PltPitSetCallback,
                         .callback = NULL,
                         .precision = 0,
                         .maxInterval = 0,
                         .private = &pitPvt};

PltHwClock_t pitClock = {.type = PLT_CLOCK_PIT,
                         .precision = 0,
                         .internalCount = 0,
                         .getTime = PltPitGetTime};

// Installs PIT interrupt
static void PltPitInstallInt()
{
    // Prepare interrupt
    NkHwInterrupt_t pitInt = {0};
    pitInt.line = PLT_PIC_IRQ_PIT;
    int vector = PltConnectInterrupt (&pitInt);
    // We always run at clock IPL
    pitInt.ipl = PLT_IPL_CLOCK;
    // Install interrupt
    PltInstallInterrupt (vector, PLT_INT_HWINT, PltPitDispatch, &pitInt);
}

// Initializes PIT clock
PltHwClock_t* PltPitInitClk()
{
    pltPitPrivate_t* pitPvt = (pltPitPrivate_t*) pitTimer.private;
    pitPvt->isPitClock = true;
    // Initialize PIT to perodic mode, with an interrupt every 10 ms
    CpuOutb (PLT_PIT_MODE_CMD, PLT_PIT_RATEGEN | PLT_PIT_LOHI | PLT_PIT_SEL_CHAN0);
    // Set divisor
    uint16_t div = PLT_PIT_FREQUENCY / PLT_PIT_HZ;
    CpuOutb (PLT_PIT_CHAN0, (uint8_t) div);
    CpuOutb (PLT_PIT_CHAN0, div >> 8);
    // Set precision of clock
    pitClock.precision = PLT_NS_IN_SEC / PLT_PIT_HZ;
    // Install interrupt handler
    PltPitInstallInt();
    return &pitClock;
}

// Initializes PIT timer part
PltHwTimer_t* PltPitInitTimer()
{
    pltPitPrivate_t* pitPvt = (pltPitPrivate_t*) pitTimer.private;
    // Check if PIT is being used as clock
    if (pitPvt->isPitClock)
    {
        // In this case, we are actually a software timer. Basically we will call the callback
        // on every tick and the callback will manually trigger each event. This is slower
        // but is neccesary for old PCs with no invariant TSC, HPET, or ACPI PM timer
        pitTimer.type = PLT_TIMER_SOFT;
        pitTimer.precision = pitClock.precision;
    }
    else
    {
        // Otherwise, put it in one-shot mode and then we will arm the timer for each event
        // This is more precise then a software clock
        CpuOutb (PLT_PIT_MODE_CMD, PLT_PIT_ONESHOT | PLT_PIT_LOHI | PLT_PIT_SEL_CHAN0);
        pitTimer.precision = PLT_NS_IN_SEC / PLT_PIT_FREQUENCY;
        pitTimer.maxInterval = UINT16_MAX * (PLT_NS_IN_SEC / PLT_PIT_FREQUENCY);
        // Install the interrupt
        PltPitInstallInt();
    }
    return &pitTimer;
}

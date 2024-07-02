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

// Sets callback of timer
static void PltPitSetCallback (void (*cb)())
{
    pitTimer.callback = cb;
}

// Gets counter
static uint64_t PltPitGetCounter()
{
    return pitTimer.counter;
}

// Arms timer to delta
static void PltPitArmTimer (uint64_t delta)
{
    // For safety raise IPL to disable interrupts
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    pitTimer.delta = delta;
    // Convert our delta to PIT precision
    delta /= PLT_PIT_FREQUENCY;
    // Ensure it is less than 16 bits
    if (delta > 0xFFFF)
        NkPanic ("nexke: can't arm PIT to non-16-bit value");
    // Set it
    CpuOutb (PLT_PIT_CHAN0, delta & 0xFF);
    CpuOutb (PLT_PIT_CHAN0, delta >> 8);
    PltLowerIpl (ipl);
}

// Private PIT dispatch handler
static bool PltPitDispatch (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Update clock counter and fire callback
    // We update the internal counter every event fire. This means that it isn't over accurate,
    // but it should be accurate enough
    pitTimer.counter += pitTimer.delta;
    // Call the timer handler if one is installed
    if (pitTimer.callback)
        pitTimer.callback();
    return true;
}

PltHwTimer_t pitTimer = {.type = PLT_TIMER_PIT,
                         .armTimer = PltPitArmTimer,
                         .getCounter = PltPitGetCounter,
                         .setCallback = PltPitSetCallback};

// Initializes PIT
PltHwTimer_t* PltPitInit()
{
    // Initialize private data to 0
    pitTimer.counter = 0;
    pitTimer.delta = 0;
    pitTimer.callback = NULL;
    // Program channel 0 to one shot
    CpuOutb (PLT_PIT_MODE_CMD, PLT_PIT_SEL_CHAN0 | PLT_PIT_ONESHOT | PLT_PIT_LOHI);
    // Set precision
    pitTimer.precision = 1000000000 / PLT_PIT_FREQUENCY;
    // Prepare interrupt
    NkHwInterrupt_t hwInt = {0};
    hwInt.line = PLT_PIC_IRQ_PIT;
    int vector = PltConnectInterrupt (&hwInt);
    // Now set IPL to special clock IPL
    hwInt.ipl = PLT_IPL_CLOCK;
    PltInstallInterrupt (vector, PLT_INT_HWINT, PltPitDispatch, &hwInt);
    return &pitTimer;
}

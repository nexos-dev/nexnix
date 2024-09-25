/*
    pcdetect.c - contains PC hardware detection
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
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>

// Console struct externs
extern NkConsole_t vgaCons;
extern NkConsole_t uartCons;
extern NkConsole_t fbCons;

static NkPlatform_t nkPlatform = {0};

// Initialize boot drivers
void PltInitDrvs()
{
    // Initialize platform
    nkPlatform.type = PLT_TYPE_PC;
    nkPlatform.subType = PLT_PC_SUBTYPE_ISA;
    NexNixBoot_t* boot = NkGetBootArgs();
    // Figure out whether we are in a graphical mode or VGA text mode
    if (boot->displayDefault)
    {
        // Initialize VGA text driver
        PltVgaInit();
        nkPlatform.primaryCons = &vgaCons;    // This is the primary console
    }
    else
    {
        NkFbConsInit();
        nkPlatform.primaryCons = &fbCons;
    }
    // Initialize UART
    if (PltUartInit())
    {
        if (!nkPlatform.primaryCons)
            nkPlatform.primaryCons = &uartCons;
        nkPlatform.secondaryCons = &uartCons;
    }
    if (!PltAcpiInit())
        return;
}

// Initialize phase 2
void PltInitPhase2()
{
    PltInitHwInts();
    PltInitInterrupts();
}

// Initialize phase 3
void PltInitPhase3()
{
    PltInitClock();
    PltInitTimer();
}

// Initializes system inerrupt controller
PltHwIntCtrl_t* PltInitHwInts()
{
    // For now all we support is 8259A PIC
    PltHwIntCtrl_t* ctrl = PltPicInit();
    nkPlatform.intCtrl = ctrl;
    return ctrl;
}

// Initializes system clock
PltHwClock_t* PltInitClock()
{
    // Try ACPI timer
    PltHwClock_t* clock = PltAcpiInitClock();
    if (!clock)
        clock = PltPitInitClk();
    nkPlatform.clock = clock;
    return clock;
}

// Initializes system timer
PltHwTimer_t* PltInitTimer()
{
    // More timers coming soon!
    PltHwTimer_t* timer = PltPitInitTimer();
    nkPlatform.timer = timer;
    return timer;
}

// Maps an MMIO range to pages and returns the page

// Gets primary console
NkConsole_t* PltGetPrimaryCons()
{
    return nkPlatform.primaryCons;
}

// Gets secondary console
NkConsole_t* PltGetSecondaryCons()
{
    return nkPlatform.secondaryCons;
}

// Gets platform
NkPlatform_t* PltGetPlatform()
{
    return &nkPlatform;
}

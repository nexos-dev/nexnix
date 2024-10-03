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

// Adds CPU to platform
void PltAddCpu (PltCpu_t* cpu)
{
    cpu->next = nkPlatform.cpus;
    if (nkPlatform.cpus)
        nkPlatform.cpus->prev = cpu;
    cpu->prev = NULL;
    nkPlatform.cpus = cpu;
    ++nkPlatform.numCpus;
}

// Adds interrupt to platform
void PltAddInterrupt (PltIntOverride_t* intSrc)
{
    intSrc->next = nkPlatform.ints;
    if (nkPlatform.ints)
        nkPlatform.ints->prev = intSrc;
    intSrc->prev = NULL;
    nkPlatform.ints = intSrc;
}

// Adds interrupt controller to platform
void PltAddIntCtrl (PltIntCtrl_t* intCtrl)
{
    intCtrl->next = nkPlatform.intCtrls;
    if (nkPlatform.intCtrls)
        nkPlatform.intCtrls->prev = intCtrl;
    intCtrl->prev = NULL;
    nkPlatform.intCtrls = intCtrl;
    ++nkPlatform.numIntCtrls;
}

bool PltAcpiSciHandler (NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Enable ACPI
void PltAcpiPcEnable()
{
    if (PltGetPlatform()->subType != PLT_PC_SUBTYPE_ACPI)
        return;
    // Get FADT
    AcpiFadt_t* fadt = (AcpiFadt_t*) PltAcpiFindTable ("FACP");
    // Check if SCI_EN is set
    if (!((CpuInw (fadt->pm1aCntBlk) | CpuInw (fadt->pm1bCntBlk)) & ACPI_SCI_EN))
    {
        CpuOutb (fadt->smiCmd, fadt->acpiEnable);    // Enable ACPI
        while (!((CpuInw (fadt->pm1aCntBlk) | CpuInw (fadt->pm1bCntBlk)) & ACPI_SCI_EN))
            ;
    }
    // Install SCI handler
    if (fadt->sciInt)
    {
        NkHwInterrupt_t sciInt = {0};
        sciInt.line = fadt->sciInt;
        sciInt.mode = PLT_MODE_LEVEL;
        int vector = PltConnectInterrupt (&sciInt);
        if (vector == -1)
            NkPanic ("nexke: unable to install SCI");
        PltInstallInterrupt (vector, PLT_INT_HWINT, PltAcpiSciHandler, &sciInt);
    }
}

// Initialize phase 2
void PltInitPhase2()
{
    PltInitInterrupts();
}

// Initialize phase 3
void PltInitPhase3()
{
    PltAcpiDetectCpus();
    PltInitHwInts();
    PltAcpiPcEnable();
    PltInitClock();
    PltInitTimer();
}

// Initializes system inerrupt controller
PltHwIntCtrl_t* PltInitHwInts()
{
    PltHwIntCtrl_t* ctrl = PltApicInit();
    if (!ctrl)
        ctrl = PltPicInit();
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

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
#include <string.h>

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
    NkLogDebug ("nexke: found CPU, interrupt controller %s, ID %d\n",
                pltCpuTypes[cpu->type],
                cpu->id);
}

// Adds interrupt to platform
void PltAddInterrupt (PltIntOverride_t* intSrc)
{
    intSrc->next = nkPlatform.ints;
    if (nkPlatform.ints)
        nkPlatform.ints->prev = intSrc;
    intSrc->prev = NULL;
    nkPlatform.ints = intSrc;
    NkLogDebug ("nexke: found interrupt override, line %d, bus %s, mode %s, GSI %u\n",
                intSrc->line,
                pltBusTypes[intSrc->bus],
                (intSrc->mode == PLT_MODE_EDGE) ? "edge" : "level",
                intSrc->gsi);
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
    NkLogDebug ("nexke: found interrupt controller, type %s, base GSI %u, address %llx\n",
                pltIntCtrlTypes[intCtrl->type],
                intCtrl->gsiBase,
                intCtrl->addr);
}

// Resolves an interrupt line from bus-specific to a GSI
uint32_t PltGetGsi (int bus, int line)
{
    if (nkPlatform.intCtrl->type == PLT_HWINT_8259A)
        return line;
    // Search through interrupt overrides
    PltIntOverride_t* intSrc = nkPlatform.ints;
    while (intSrc)
    {
        // Check if this is it
        if (intSrc->bus == bus && intSrc->line == line)
            return intSrc->gsi;
        intSrc = intSrc->next;
    }
    return line;
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
        NkLogDebug ("nexke: enabled ACPI\n");
    }
    else
        NkLogDebug ("nexke: ACPI already enabled\n");
    // Install SCI handler
    if (fadt->sciInt && !NkReadArg ("-nosci"))
    {
        NkHwInterrupt_t* sciInt = PltAllocHwInterrupt();
        memset (sciInt, 0, sizeof (NkHwInterrupt_t));
        sciInt->gsi = PltGetGsi (PLT_BUS_ISA, fadt->sciInt);
        sciInt->mode = PLT_MODE_LEVEL;
        sciInt->flags = PLT_HWINT_ACTIVE_LOW;
        int vector = PltConnectInterrupt (sciInt);
        if (vector == -1)
            NkPanic ("nexke: unable to install SCI");
        PltInstallInterrupt (vector, PLT_INT_HWINT, PltAcpiSciHandler, sciInt);
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
    CpuEnable();
    CpuUnholdInts();
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
    PltHwClock_t* clock = NULL;
    // Check if we're allowed to use ACPI clock
    if (NkReadArg ("-nosci"))
        NkLogDebug ("nexke: ACPI PM use suppressed by -nosci\n");
    else
        clock = PltAcpiInitClock();
    if (!clock)
        clock = PltPitInitClk();
    nkPlatform.clock = clock;
    return clock;
}

// Initializes system timer
PltHwTimer_t* PltInitTimer()
{
    PltHwTimer_t* timer = PltApicInitTimer();
    if (!timer)
        timer = PltPitInitTimer();
    nkPlatform.timer = timer;
    return timer;
}

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

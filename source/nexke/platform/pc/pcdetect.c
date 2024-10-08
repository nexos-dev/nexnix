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

#include <assert.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/pc.h>
#include <stdlib.h>
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
    // See if ACPI is allowed
    if (!NkReadArg ("-noacpi"))
        PltAcpiInit();
}

// Adds CPU to platform
void PltAddCpu (PltCpu_t* cpu)
{
    cpu->prev = nkPlatform.cpus;
    if (nkPlatform.cpusEnd)
        nkPlatform.cpusEnd->next = cpu;
    cpu->next = NULL;
    nkPlatform.cpusEnd = cpu;
    if (!nkPlatform.cpus)
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
    NkLogDebug ("nexke: found interrupt override, line %d, bus %s, mode %s, polarity %s, GSI %u\n",
                intSrc->line,
                pltBusTypes[intSrc->bus],
                (intSrc->mode == PLT_MODE_EDGE) ? "edge" : "level",
                (intSrc->polarity == PLT_POL_ACTIVE_HIGH) ? "high" : "low",
                intSrc->gsi);
}

// Adds interrupt controller to platform
void PltAddIntCtrl (PltIntCtrl_t* intCtrl)
{
    intCtrl->prev = nkPlatform.intCtrlsEnd;
    if (nkPlatform.intCtrlsEnd)
        nkPlatform.intCtrlsEnd->next = intCtrl;
    intCtrl->next = NULL;
    nkPlatform.intCtrlsEnd = intCtrl;
    if (!nkPlatform.intCtrls)
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

// Detects CPUs if there is no MPS or ACPI
void PltFallbackDetectCpus()
{
    // Check if CPUID reports APIC
    if (CpuGetFeatures() & CPU_FEATURE_APIC)
    {
        // Create a CPU
        PltCpu_t* cpu = (PltCpu_t*) malloc (sizeof (PltCpu_t));
        assert (cpu);
        cpu->id = 0;
        cpu->type = PLT_CPU_APIC;
        PltAddCpu (cpu);
        // Create a interrupt controller
        PltIntCtrl_t* ctrl =
            (PltIntCtrl_t*) malloc (sizeof (PltIntCtrl_t));    // We can't access the normal cache
        ctrl->addr = PLT_IOAPIC_BASE;
        ctrl->gsiBase = 0;
        ctrl->type = PLT_INTCTRL_IOAPIC;
        PltAddIntCtrl (ctrl);    // Add it to the system
        // Create override from INT2 to INT0
        PltIntOverride_t* intOv = (PltIntOverride_t*) malloc (sizeof (PltIntCtrl_t));
        intOv->bus = PLT_BUS_ISA;
        intOv->gsi = 2;
        intOv->line = 0;
        intOv->mode = PLT_MODE_EDGE;
        PltAddInterrupt (intOv);
    }
    else
    {
        // Create a CPU
        PltCpu_t* cpu = (PltCpu_t*) malloc (sizeof (PltCpu_t));
        assert (cpu);
        cpu->id = 0;
        cpu->type = PLT_CPU_UP;
        PltAddCpu (cpu);
        // Create a interrupt controller
        PltIntCtrl_t* ctrl =
            (PltIntCtrl_t*) malloc (sizeof (PltIntCtrl_t));    // We can't access the normal cache
        ctrl->addr = 0;
        ctrl->gsiBase = 0;
        ctrl->type = PLT_INTCTRL_8259A;
        PltAddIntCtrl (ctrl);    // Add it to the system
    }
}

bool PltAcpiSciHandler (NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Initialize phase 2
void PltInitPhase2()
{
    PltInitInterrupts();
}

// Initialize phase 3
void PltInitPhase3()
{
    if (!PltAcpiDetectCpus())
    {
        NkLogDebug ("nexke: ACPI not supported\n");
        if (!PltMpsDetectCpus())
        {
            NkLogDebug ("nexke: MPS not supported\n");
            PltFallbackDetectCpus();    // Call the fallback function
        }
    }
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
    // Try TSC first
    if (NkReadArg ("-notsc"))
        NkLogDebug ("nexke: TSC use suppressed by -notsc\n");
    else
        clock = CpuInitTscClock();
    if (!clock)
    {
        // Try HPET
        clock = PltHpetInitClock();
        if (!clock)
        {
            // Check if ACPI clock use is allowed
            if (NkReadArg ("-nosci"))
                NkLogDebug ("nexke: ACPI PM use suppressed by -nosci\n");
            else
                clock = PltAcpiInitClock();
            if (!clock)
                clock = PltPitInitClk();
        }
    }
    assert (clock);
    nkPlatform.clock = clock;
    return clock;
}

// Initializes system timer
PltHwTimer_t* PltInitTimer()
{
    PltHwTimer_t* timer = NULL;
    // Only do HPET timer with HPET clock
    if (nkPlatform.clock->type == PLT_CLOCK_HPET)
        timer = PltHpetInitTimer();
    if (!timer)
    {
        timer = PltApicInitTimer();
        if (!timer)
            timer = PltPitInitTimer();
    }
    assert (timer);
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

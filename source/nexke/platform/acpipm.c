/*
    acpipm.c - contains ACPI PM timer driver
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
#include <string.h>

extern PltHwClock_t acpiPmClock;

// PM clock base
static AcpiGas_t acpiPmGas = {0};

// MMIO base if using MMIO
static uint32_t* acpiPmBase = NULL;

// Overflow counter
static int overFlowCount = 0;

// Is counter 32 bit?
static bool is32Bit = false;

#define ACPI_PM_FREQ 3579545

static uint64_t PltAcpiGetTime()
{
    uint32_t val = 0;
    if (acpiPmGas.asId == ACPI_GAS_IO)
        val = CpuInl (acpiPmGas.addr);
    else
        val = *acpiPmBase;
    return ((overFlowCount << ((is32Bit) ? 32ULL : 24)) + val) * acpiPmClock.precision;
}

static void PltAcpiPoll (uint64_t time)
{
    uint64_t target = time + PltAcpiGetTime();
    target /= acpiPmClock.precision;
    while (1)
    {
        uint32_t val = 0;
        if (acpiPmGas.asId == ACPI_GAS_IO)
            val = CpuInl (acpiPmGas.addr);
        else
            val = *acpiPmBase;
        val = ((overFlowCount << ((is32Bit) ? 32ULL : 24)) + val);
        if (val >= target)
            break;
    }
}

PltHwClock_t acpiPmClock = {.type = PLT_CLOCK_ACPI,
                            .precision = 0,
                            .internalCount = 0,
                            .getTime = PltAcpiGetTime,
                            .poll = PltAcpiPoll};

// Initializes ACPI PM timer
PltHwClock_t* PltAcpiInitClock()
{
    // Grab the FADT
    AcpiFadt_t* fadt = (AcpiFadt_t*) PltAcpiFindTable ("FACP");
    if (!fadt)
        return NULL;    // Doesn't exist
    if (!fadt->pmTmrLen)
        return NULL;    // ACPI doesn't support PM timer
    // Create timer GAS
    if (PltGetPlatform()->acpiVer > 1)
        memcpy (&acpiPmGas, &fadt->xPmTmrBlk, sizeof (AcpiGas_t));
    else
    {
        acpiPmGas.accessSz = 4;
        acpiPmGas.addr = fadt->pmTmrBlk;
        acpiPmGas.asId = ACPI_GAS_IO;
    }
    if (fadt->flags & ACPI_FADT_TMR_32BIT)
    {
        NkLogDebug ("nexke: using 32 bit ACPI PM\n");
        is32Bit = true;
    }
    // Check if we need to map it
    if (acpiPmGas.asId == ACPI_GAS_MEM)
    {
        acpiPmBase =
            MmAllocKvMmio ((void*) acpiPmGas.addr, 1, MUL_PAGE_R | MUL_PAGE_KE | MUL_PAGE_CD);
    }
    acpiPmClock.precision = PLT_NS_IN_SEC / ACPI_PM_FREQ;
    NkLogDebug ("nexke: using ACPI as clock, precision %uns\n", acpiPmClock.precision);
    return &acpiPmClock;
}

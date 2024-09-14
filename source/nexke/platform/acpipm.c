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
#include <nexke/nexke.h>
#include <nexke/platform.h>

extern PltHwClock_t acpiPmClock;

// PM clock base
static AcpiGas_t* nkAcpiPmGas = NULL;

static uint64_t PltAcpiGetTime()
{
}

PltHwClock_t acpiPmClock = {.type = PLT_CLOCK_ACPI,
                            .precision = 0,
                            .internalCount = 0,
                            .getTime = PltAcpiGetTime};

// Initializes ACPI PM timer
PltHwClock_t* PltAcpiInitClock()
{
    // Grab the FADT
    AcpiFadt_t* fadt = (AcpiFadt_t*) PltAcpiFindTable ("FACP");
    if (!fadt)
        return NULL;    // Doesn't exist
    if (!fadt->pmTmrLen)
        return NULL;    // ACPI doesn't support PM timer
}

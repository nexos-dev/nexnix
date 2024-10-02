/*
    acpipc.c - contains PC specific ACPI parts
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

bool PltAcpiSciHandler (NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Enable ACPI
void PltAcpiPcEnable()
{
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

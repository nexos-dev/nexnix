/*
    acpi.c - contains ACPI table parser
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
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <string.h>

// Memory for RSDT
static MmSpaceEntry_t* rsdtMem = NULL;

// Initializes ACPI
bool PltAcpiInit()
{
    NexNixBoot_t* boot = NkGetBootArgs();
    // Find ACPI component
    if (!(boot->detectedComps & (1 << NB_TABLE_ACPI)))
        return false;    // ACPI doesn't exist
    // Parse RSDP
    AcpiRsdp_t* rsdp = (AcpiRsdp_t*) boot->comps[NB_TABLE_ACPI];
    // Check validity
    if (memcmp (rsdp, "RSD PTR ", 8) != 0)
        return false;
    // Check verions
    if (rsdp->rev >= 2)
    {
        PltGetPlatform()->acpiVer = 2;
        PltGetPlatform()->rsdt = (AcpiSdt_t*) ((uintptr_t) rsdp->xsdtAddr);
    }
    else
    {
        PltGetPlatform()->acpiVer = 1;
        PltGetPlatform()->rsdt = (AcpiSdt_t*) ((uintptr_t) rsdp->rsdtAddr);
    }
    // Say we are ACPI
    PltGetPlatform()->subType = PLT_PC_SUBTYPE_ACPI;
    return true;
}

// Finds an ACPI table
AcpiSdt_t* PltAcpiFindTable (const char* sig)
{
    assert (strlen (sig) == 4);
    if (PltGetPlatform()->subType != PLT_PC_SUBTYPE_ACPI)
        return NULL;
    AcpiSdt_t* rsdt = PltGetPlatform()->rsdt;
    // Check wheter to look through XSDT or RSDT
    if (PltGetPlatform()->acpiVer >= 2)
    {
        // Get number of entries
        size_t numEnt = (rsdt->length - sizeof (AcpiSdt_t)) / sizeof (uint64_t);
        uint64_t* entList = (uint64_t*) (rsdt + 1);
        for (int i = 0; i < numEnt; ++i)
        {
            AcpiSdt_t* curSdt = (AcpiSdt_t*) ((uintptr_t) entList[i]);
            if (!memcmp (curSdt->sig, sig, 4))
                return curSdt;
        }
    }
    else
    {
        // Get number of entries
        size_t numEnt = (rsdt->length - sizeof (AcpiSdt_t)) / sizeof (uint32_t);
        uint32_t* entList = (uint32_t*) (rsdt + 1);
        for (int i = 0; i < numEnt; ++i)
        {
            AcpiSdt_t* curSdt = (AcpiSdt_t*) ((uintptr_t) entList[i]);
            if (!memcmp (curSdt->sig, sig, 4))
                return curSdt;
        }
    }
    return NULL;
}

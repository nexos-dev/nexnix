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
    PltGetPlatform()->acpiVer = rsdp->rev;
    memcpy (&PltGetPlatform()->rsdp, rsdp, sizeof (AcpiRsdp_t));
    // Say we are ACPI
    PltGetPlatform()->subType = PLT_PC_SUBTYPE_ACPI;
    return true;
}

// Finds entry in table cache
static AcpiCacheEnt_t* pltAcpiFindCache (const char* sig)
{
    AcpiCacheEnt_t* curEnt = PltGetPlatform()->tableCache;
    while (curEnt)
    {
        if (!strcmp (curEnt->table.sig, sig))    // Check signature
            return curEnt;
        curEnt = curEnt->next;
    }
    return NULL;    // Table not cached
}

// Gets table from firmware
static void* pltAcpiFindTableFw (const char* sig)
{
    // Special case for RSDT or XSDT
    if (!strcmp (sig, "XSDT"))
    {
        // Get from RSDP
        AcpiRsdp_t* rsdp = &PltGetPlatform()->rsdp;
        if (rsdp->rev < 2)
            return NULL;
        return (void*) rsdp->xsdtAddr;
    }
    else if (!strcmp (sig, "RSDT"))
        return (void*) PltGetPlatform()->rsdp.rsdtAddr;
    // Otherwise get the RSDT or XSDT and search it
    AcpiSdt_t* xsdt = PltAcpiFindTable ("XSDT");
    if (xsdt)
    {
        // Get number of entries
        size_t numEntries = (xsdt->length / sizeof (uint64_t)) - sizeof (AcpiSdt_t);
        uint64_t* tables =
            (uint64_t*) ((uintptr_t) xsdt + sizeof (AcpiSdt_t));    // Get pointer to table array
        for (int i = 0; i < numEntries; ++i)
        {
        }
    }
}

// Finds an ACPI table
AcpiSdt_t* PltAcpiFindTable (const char* sig)
{
    assert (strlen (sig) == 4);
    // Check if this is an ACPI system
    if (PltGetPlatform()->subType != PLT_PC_SUBTYPE_ACPI)
        return NULL;
    // First we need to check the cache for an entry
    AcpiCacheEnt_t* ent = pltAcpiFindCache (sig);
    if (ent)
        return &ent->table;    // We're done
    // Table not cached, we first need to get the table from the FW
    // and map, and then add it to the cache
    void* tablePhys = pltAcpiFindTableFw (sig);
    return NULL;
}

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

static SlabCache_t* acpiCache = NULL;

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
    // Setup cache
    acpiCache = MmCacheCreate (sizeof (AcpiCacheEnt_t), NULL, NULL);
    assert (acpiCache);
    return true;
}

// Finds entry in table cache
static AcpiCacheEnt_t* pltAcpiFindCache (const char* sig)
{
    AcpiCacheEnt_t* curEnt = PltGetPlatform()->tableCache;
    while (curEnt)
    {
        if (!strcmp (curEnt->table->sig, sig))    // Check signature
            return curEnt;
        curEnt = curEnt->next;
    }
    return NULL;    // Table not cached
}

// Caches ACPI table
static void pltAcpiCacheTable (AcpiSdt_t* sdt)
{
    // Create cache entry
    AcpiCacheEnt_t* cacheEnt = MmCacheAlloc (acpiCache);
    if (!cacheEnt)
        NkPanicOom();
    cacheEnt->table = sdt;
    cacheEnt->next = PltGetPlatform()->tableCache;
    PltGetPlatform()->tableCache = cacheEnt;
}

// Gets table from firmware
static void* pltAcpiFindTableFw (const char* sig, uint32_t* len)
{
    // Special case for RSDT or XSDT
    if (!strcmp (sig, "XSDT"))
    {
        // Get from RSDP
        AcpiRsdp_t* rsdp = &PltGetPlatform()->rsdp;
        if (rsdp->rev < 2)
            return NULL;
        // Map it so we have the length
        AcpiSdt_t* sdt = MmAllocKvMmio ((void*) rsdp->xsdtAddr, 1, MUL_PAGE_KE | MUL_PAGE_R);
        if (!sdt)
            NkPanicOom();
        *len = sdt->length;
        MmFreeKvMmio (sdt);
        return (void*) rsdp->xsdtAddr;
    }
    else if (!strcmp (sig, "RSDT"))
    {
        // Map it so we have the length
        AcpiSdt_t* sdt =
            MmAllocKvMmio ((void*) PltGetPlatform()->rsdp.rsdtAddr, 1, MUL_PAGE_KE | MUL_PAGE_R);
        if (!sdt)
            NkPanicOom();
        *len = sdt->length;
        MmFreeKvMmio (sdt);
        return (void*) PltGetPlatform()->rsdp.rsdtAddr;
    }
    // Otherwise get the RSDT or XSDT and search it
    AcpiSdt_t* xsdt = PltAcpiFindTable ("XSDT");
    if (xsdt)
    {
        // Get number of entries
        size_t numEntries = (xsdt->length - sizeof (AcpiSdt_t)) / sizeof (uint64_t);
        uint64_t* tables =
            (uint64_t*) ((uintptr_t) xsdt + sizeof (AcpiSdt_t));    // Get pointer to table array
        for (int i = 0; i < numEntries; ++i)
        {
            void* table = (void*) tables[i];
            // Map this table
            AcpiSdt_t* sdt = (AcpiSdt_t*) MmAllocKvMmio (table, 1, MUL_PAGE_KE | MUL_PAGE_R);
            if (!sdt)
                NkPanicOom();
            if (!memcmp (sdt->sig, sig, 4))
            {
                // Unmap and return, and set length
                *len = sdt->length;
                MmFreeKvMmio (sdt);
                return table;
            }
        }
    }
    else
    {
        AcpiSdt_t* rsdt = PltAcpiFindTable ("RSDT");
        assert (rsdt);
        // Get number of entries
        size_t numEntries = (rsdt->length - sizeof (AcpiSdt_t)) / sizeof (uint32_t);
        uint32_t* tables =
            (uint32_t*) ((uintptr_t) rsdt + sizeof (AcpiSdt_t));    // Get pointer to table array
        for (int i = 0; i < numEntries; ++i)
        {
            void* table = (void*) tables[i];
            // Map this table
            AcpiSdt_t* sdt = (AcpiSdt_t*) MmAllocKvMmio (table, 1, MUL_PAGE_KE | MUL_PAGE_R);
            if (!sdt)
                NkPanicOom();
            if (!memcmp (sdt->sig, sig, 4))
            {
                // Unmap and return
                *len = sdt->length;
                MmFreeKvMmio (sdt);
                return (void*) table;
            }
        }
    }
    return NULL;
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
        return ent->table;    // We're done
    // Table not cached, we first need to get the table from the FW
    // and map, and then add it to the cache
    uint32_t len = 0;
    void* tablePhys = pltAcpiFindTableFw (sig, &len);
    if (!tablePhys)
        return NULL;
    // Map it and then copy it
    AcpiSdt_t* fwTable = MmAllocKvMmio (tablePhys,
                                        CpuPageAlignUp (len) / NEXKE_CPU_PAGESZ,
                                        MUL_PAGE_KE | MUL_PAGE_R);
    if (!fwTable)
        NkPanicOom();
    AcpiSdt_t* res = MmAllocKvRegion (CpuPageAlignUp (len) / NEXKE_CPU_PAGESZ, MM_KV_NO_DEMAND);
    if (!res)
        NkPanicOom();
    memcpy (res, fwTable, len);
    MmFreeKvMmio (fwTable);
    pltAcpiCacheTable (res);
    return res;
}

// SCI handler
bool PltAcpiSciHandler (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    return true;
}

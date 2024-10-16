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
static SlabCache_t* cpuCache = NULL;
static SlabCache_t* intCache = NULL;
static SlabCache_t* intCtrlCache = NULL;

static AcpiFadt_t* fadt = NULL;

// PM timer stuff
extern PltHwClock_t acpiPmClock;

// MMIO base if using MMIO
static uint32_t* acpiPmBase = NULL;

// Overflow counter
static int overFlowCount = 0;

// Overflow indicator
static bool overFlowSoon = false;
static int overFlowTimes = 0;
static uint32_t overFlowRead = 0;

// Is counter 32 bit?
static bool is32Bit = false;

#define ACPI_PM_FREQ 3579545

// Initializes ACPI
bool PltAcpiInit()
{
    NexNixBoot_t* boot = NkGetBootArgs();
    // Find ACPI component
    if (!(boot->detectedComps & (1 << NB_TABLE_ACPI)))
    {
        return false;    // ACPI doesn't exist
    }
    // Parse RSDP
    AcpiRsdp_t* rsdp = (AcpiRsdp_t*) boot->comps[NB_TABLE_ACPI];
    // Check validity
    if (memcmp (rsdp, "RSD PTR ", 8) != 0)
        return false;
    // Check ACPI 1 part checksum
    if (!NkVerifyChecksum ((uint8_t*) rsdp, 20))
        return false;
    // Check ACPI 2 part
    if (rsdp->rev >= 2)
    {
        if (!NkVerifyChecksum ((uint8_t*) rsdp, sizeof (AcpiRsdp_t)))
            return false;
    }
    PltGetPlatform()->acpiVer = rsdp->rev;
    memcpy (&PltGetPlatform()->rsdp, rsdp, sizeof (AcpiRsdp_t));
    // Say we are ACPI
    PltGetPlatform()->subType = PLT_PC_SUBTYPE_ACPI;
    // Setup cache
    acpiCache = MmCacheCreate (sizeof (AcpiCacheEnt_t), "AcpiCacheEnt_t", 0, 0);
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
static paddr_t pltAcpiFindTableFw (const char* sig, uint32_t* len)
{
    // Special case for RSDT or XSDT
    if (!strcmp (sig, "XSDT"))
    {
        // Get from RSDP
        AcpiRsdp_t* rsdp = &PltGetPlatform()->rsdp;
        if (rsdp->rev < 2)
            return 0;
        // Map it so we have the length
        AcpiSdt_t* sdt = MmAllocKvMmio (rsdp->xsdtAddr, 1, MUL_PAGE_KE | MUL_PAGE_R);
        if (!sdt)
            NkPanicOom();
        *len = sdt->length;
        MmFreeKvMmio (sdt);
        return (paddr_t) rsdp->xsdtAddr;
    }
    else if (!strcmp (sig, "RSDT"))
    {
        // Map it so we have the length
        AcpiSdt_t* sdt =
            MmAllocKvMmio (PltGetPlatform()->rsdp.rsdtAddr, 1, MUL_PAGE_KE | MUL_PAGE_R);
        if (!sdt)
            NkPanicOom();
        *len = sdt->length;
        MmFreeKvMmio (sdt);
        return PltGetPlatform()->rsdp.rsdtAddr;
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
            paddr_t table = (paddr_t) tables[i];
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
            paddr_t table = (paddr_t) tables[i];
            // Map this table
            AcpiSdt_t* sdt = (AcpiSdt_t*) MmAllocKvMmio (table, 1, MUL_PAGE_KE | MUL_PAGE_R);
            if (!sdt)
                NkPanicOom();
            if (!memcmp (sdt->sig, sig, 4))
            {
                // Unmap and return
                *len = sdt->length;
                MmFreeKvMmio (sdt);
                return table;
            }
        }
    }
    return 0;
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
    paddr_t tablePhys = pltAcpiFindTableFw (sig, &len);
    if (!tablePhys)
        return NULL;
    // Map it and then copy it
    AcpiSdt_t* fwTable = MmAllocKvMmio (tablePhys,
                                        CpuPageAlignUp (len) / NEXKE_CPU_PAGESZ,
                                        MUL_PAGE_KE | MUL_PAGE_R);
    if (!fwTable)
        NkPanicOom();
    // Check checksum
    if (!NkVerifyChecksum ((uint8_t*) fwTable, fwTable->length))
    {
        MmFreeKvMmio (fwTable);
        return NULL;
    }
    AcpiSdt_t* res = MmAllocKvRegion (CpuPageAlignUp (len) / NEXKE_CPU_PAGESZ, MM_KV_NO_DEMAND);
    if (!res)
        NkPanicOom();
    memcpy (res, fwTable, len);
    MmFreeKvMmio (fwTable);
    pltAcpiCacheTable (res);
    return res;
}

// Detects all CPUs attached to the platform
bool PltAcpiDetectCpus()
{
    if (PltGetPlatform()->subType != PLT_PC_SUBTYPE_ACPI)
        return false;
    // Create slab cache for CPUs
    cpuCache = MmCacheCreate (sizeof (PltCpu_t), "PltCpu_t", 0, 0);
    intCache = MmCacheCreate (sizeof (PltIntOverride_t), "PltIntOverride_t", 0, 0);
    intCtrlCache = MmCacheCreate (sizeof (PltIntCtrl_t), "PltIntCtrl_t", 0, 0);
    // Get the MADT
    AcpiMadt_t* madt = (AcpiMadt_t*) PltAcpiFindTable ("APIC");
    if (!madt)
        return false;
    uint32_t len = madt->sdt.length - sizeof (AcpiMadt_t);
    AcpiMadtEntry_t* cur = (AcpiMadtEntry_t*) (madt + 1);
    for (int i = 0; i < len;)
    {
        // See what this is
        if (cur->type == ACPI_MADT_LAPIC)
        {
            // Prepare a CPU
            AcpiLapic_t* lapic = (AcpiLapic_t*) cur;
            if ((lapic->flags & ACPI_LAPIC_ENABLED) || (lapic->flags & ACPI_LAPIC_ONLINE_CAP))
            {
                PltCpu_t* cpu = MmCacheAlloc (cpuCache);
                cpu->id = lapic->id;
                cpu->type = PLT_CPU_APIC;
                PltAddCpu (cpu);
            }
        }
        else if (cur->type == ACPI_MADT_X2APIC)
        {
            // Prepare a CPU
            AcpiX2Apic_t* lapic = (AcpiX2Apic_t*) cur;
            if ((lapic->flags & ACPI_LAPIC_ENABLED) || (lapic->flags & ACPI_LAPIC_ONLINE_CAP))
            {
                PltCpu_t* cpu = MmCacheAlloc (cpuCache);
                cpu->id = lapic->id;
                cpu->type = PLT_CPU_X2APIC;
                PltAddCpu (cpu);
            }
        }
        else if (cur->type == ACPI_MADT_IOAPIC)
        {
            // Prepare an I/O APIC
            AcpiIoApic_t* ioapic = (AcpiIoApic_t*) cur;
            PltIntCtrl_t* intCtrl = MmCacheAlloc (intCtrlCache);
            intCtrl->addr = ioapic->addr;
            intCtrl->gsiBase = ioapic->gsiBase;
            intCtrl->type = PLT_INTCTRL_IOAPIC;
            intCtrl->id = ioapic->id;
            PltAddIntCtrl (intCtrl);
        }
        else if (cur->type == ACPI_MADT_ISO)
        {
            // Prepare an interrupt override
            AcpiIso_t* iso = (AcpiIso_t*) cur;
            PltIntOverride_t* intSrc = MmCacheAlloc (intCache);
            intSrc->bus = PLT_BUS_ISA;
            intSrc->gsi = iso->gsi;
            intSrc->line = iso->line;
            // Set trigger mode
            if (iso->flags & ACPI_ISO_LEVEL)
                intSrc->mode = PLT_MODE_LEVEL;
            else if (iso->flags & ACPI_ISO_EDGE)
                intSrc->mode = PLT_MODE_EDGE;
            else
            {
                if (intSrc->bus == PLT_BUS_ISA)
                    intSrc->mode = PLT_MODE_EDGE;
            }
            // Set polarity
            if (iso->flags & ACPI_ISO_ACTIVE_LOW)
                intSrc->polarity = PLT_POL_ACTIVE_LOW;
            else if (iso->flags & ACPI_ISO_ACTIVE_HIGH)
                intSrc->polarity = PLT_POL_ACTIVE_HIGH;
            else
            {
                if (intSrc->bus == PLT_BUS_ISA)
                {
                    if (intSrc->mode == PLT_MODE_EDGE)
                        intSrc->polarity = PLT_POL_ACTIVE_HIGH;
                    else
                        intSrc->polarity = PLT_POL_ACTIVE_LOW;
                }
            }
            PltAddInterrupt (intSrc);
        }
        // To next entry
        i += cur->length;
        cur = (void*) cur + cur->length;
    }
    return true;
}

// ACPI register handling
#define ACPI_REG_PM1_STS  0
#define ACPI_REG_PM1_EN   1
#define ACPI_REG_PM1_CTL  2
#define ACPI_REG_PM_TMR   3
#define ACPI_REG_PM2      4
#define ACPI_REG_GPE0_STS 5
#define ACPI_REG_GPE0_EN  6
#define ACPI_REG_GPE1_STS 7
#define ACPI_REG_GPE1_EN  8
#define ACPI_REG_MAX      9

typedef struct _acpireg
{
    uint64_t addr;     // Address of register
    size_t sz;         // Access size
    size_t offset;     // Offset from address to access
    int type;          // Type of access
    uint64_t addrB;    // For register groupings
} AcpiReg_t;

static AcpiReg_t acpiRegs[ACPI_REG_MAX] = {0};

static uint64_t pltAcpiMapReg (AcpiGas_t* gas)
{
    if (gas->asId == ACPI_GAS_MEM)
    {
        return (uint64_t) MmAllocKvMmio ((paddr_t) gas->addr,
                                         1,
                                         MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_CD | MUL_PAGE_RW);
    }
    else if (gas->asId == ACPI_GAS_IO)
        return gas->addr;
    else
        NkPanic ("nexke: unsupported GAS type for ACPI register\n");
}

static void pltAcpiMapRegs()
{
    // Set up PM1_STS
    uint64_t addr = 0, addrB = 0;
    AcpiReg_t* curReg = acpiRegs;
    // Check ACPI 2+ GAS
    if (fadt->xPm1aEvtBlk.addr)
    {
        curReg->type = fadt->xPm1aEvtBlk.asId;
        addr = pltAcpiMapReg (&fadt->xPm1aEvtBlk);
        // Check for B side
        if (fadt->xPm1bEvtBlk.addr)
        {
            assert (fadt->xPm1bEvtBlk.asId == curReg->type);
            addrB = pltAcpiMapReg (&fadt->xPm1bEvtBlk);
        }
    }
    else
    {
        curReg->type = ACPI_GAS_IO;
        addr = fadt->pm1aEvtBlk;
        addrB = fadt->pm1bEvtBlk;
    }
    // Set up register now
    curReg->addr = addr;
    curReg->addrB = addrB;
    curReg->offset = 0;
    curReg->sz = fadt->pm1EvtLen / 2;
    // Move to PM1_EN
    ++curReg;
    curReg->addr = addr;
    curReg->type = (curReg - 1)->type;    // Same type as last register
    curReg->offset = fadt->pm1EvtLen / 2;
    curReg->sz = fadt->pm1EvtLen / 2;
    curReg->addrB = addrB;
    // Move to PM1_CNT
    ++curReg;
    // Check ACPI 2+ GAS
    if (fadt->xPm1aCntBlk.addr)
    {
        curReg->type = fadt->xPm1aCntBlk.asId;
        curReg->addr = pltAcpiMapReg (&fadt->xPm1aCntBlk);
        // Check for B side
        if (fadt->xPm1bCntBlk.addr)
        {
            assert (fadt->xPm1bCntBlk.asId == curReg->type);
            curReg->addrB = pltAcpiMapReg (&fadt->xPm1bCntBlk);
        }
    }
    else
    {
        curReg->type = ACPI_GAS_IO;
        curReg->addr = fadt->pm1aCntBlk;
        curReg->addrB = fadt->pm1bCntBlk;
    }
    curReg->sz = fadt->pm1CntLen;
    // Move to PM_TMR
    ++curReg;
    if (fadt->xPmTmrBlk.addr)
    {
        curReg->type = fadt->xPmTmrBlk.asId;
        curReg->addr = pltAcpiMapReg (&fadt->xPmTmrBlk);
    }
    else
    {
        curReg->type = ACPI_GAS_IO;
        curReg->addr = fadt->pmTmrBlk;
    }
    curReg->sz = fadt->pmTmrLen;
    // Move to PM2
    ++curReg;
    if (fadt->xPm2CntBlk.addr)
    {
        curReg->type = fadt->xPm2CntBlk.asId;
        curReg->addr = pltAcpiMapReg (&fadt->xPm2CntBlk);
    }
    else
    {
        curReg->type = ACPI_GAS_IO;
        curReg->addr = fadt->pm2CntBlk;
    }
    curReg->sz = fadt->pm2CntLen;
    // Move to GPE0
    uint64_t addrGpe = 0;
    ++curReg;
    if (fadt->xGpe0Blk.addr)
    {
        curReg->type = fadt->xGpe0Blk.asId;
        addrGpe = pltAcpiMapReg (&fadt->xGpe0Blk);
    }
    else
    {
        curReg->type = ACPI_GAS_IO;
        addrGpe = fadt->gpe0Blk;
    }
    curReg->sz = fadt->gpe0Len / 2;
    curReg->addr = addrGpe;
    // GPE0_EN
    ++curReg;
    curReg->addr = addrGpe;
    curReg->type = (curReg - 1)->type;
    curReg->offset = fadt->gpe0Len / 2;
    curReg->sz = fadt->gpe0Len / 2;
    // GPE1
    ++curReg;
    if (fadt->xGpe1Blk.addr)
    {
        curReg->type = fadt->xGpe1Blk.asId;
        addrGpe = pltAcpiMapReg (&fadt->xGpe1Blk);
    }
    else
    {
        curReg->type = ACPI_GAS_IO;
        addrGpe = fadt->gpe1Blk;
    }
    curReg->sz = fadt->gpe1Len / 2;
    curReg->addr = addrGpe;
    // GPE1_EN
    ++curReg;
    curReg->addr = addrGpe;
    curReg->type = (curReg - 1)->type;
    curReg->offset = fadt->gpe1Len / 2;
    curReg->sz = fadt->gpe1Len / 2;
}

static inline uint32_t pltAcpiReadReg (int regIdx, size_t offset)
{
    assert (regIdx < ACPI_REG_MAX);
    AcpiReg_t* reg = &acpiRegs[regIdx];
    uint32_t val = 0;
    if (reg->type == ACPI_GAS_IO)
    {
        // Check size
        if (reg->sz == 2)
        {
            val = CpuInw (reg->addr + reg->offset + offset);
            if (reg->addrB)
                val |= CpuInw (reg->addrB + reg->offset + offset);
        }
        else if (reg->sz >= 4)
        {
            val = CpuInl (reg->addr + reg->offset);
            if (reg->addrB)
                val |= CpuInw (reg->addrB + reg->offset + offset);
        }
        else
            assert (0);
    }
    else if (reg->type == ACPI_GAS_MEM)
    {
        // Check size
        if (reg->sz == 2)
        {
            assert (offset % 2 == 0);
            volatile uint16_t* regBase = (volatile uint16_t*) (reg->addr + reg->offset + offset);
            val = *regBase;
            if (reg->addrB)
            {
                volatile uint16_t* regBaseB =
                    (volatile uint16_t*) (reg->addrB + reg->offset + offset);
                val |= *regBaseB;
            }
        }
        else if (reg->sz >= 4)
        {
            assert (offset % 4 == 0);
            volatile uint32_t* regBase = (volatile uint32_t*) (reg->addr + reg->offset + offset);
            val = *regBase;
            if (reg->addrB)
            {
                volatile uint32_t* regBaseB =
                    (volatile uint32_t*) (reg->addrB + reg->offset + offset);
                val |= *regBaseB;
            }
        }
        else
            assert (0);
    }
    else
        assert (0);
    return val;
}

static inline void pltAcpiWriteReg (int regIdx, uint32_t val, size_t offset)
{
    assert (regIdx < ACPI_REG_MAX);
    AcpiReg_t* reg = &acpiRegs[regIdx];
    if (reg->type == ACPI_GAS_IO)
    {
        // Check size
        if (reg->sz == 2)
        {
            CpuOutw (reg->addr + reg->offset + offset, val);
            if (reg->addrB)
                CpuOutw (reg->addrB + reg->offset + offset, val);
        }
        else if (reg->sz >= 4)
        {
            CpuOutl (reg->addr + reg->offset + offset, val);
            if (reg->addrB)
                CpuOutl (reg->addrB + reg->offset + offset, val);
        }
        else
            assert (0);
    }
    else if (reg->type == ACPI_GAS_MEM)
    {
        // Check size
        if (reg->sz == 2)
        {
            assert (offset % 2 == 0);
            volatile uint16_t* regBase = (volatile uint16_t*) (reg->addr + reg->offset + offset);
            *regBase = val;
            if (reg->addrB)
            {
                volatile uint16_t* regBaseB =
                    (volatile uint16_t*) (reg->addrB + reg->offset + offset);
                *regBaseB = val;
            }
        }
        else if (reg->sz >= 4)
        {
            assert (offset % 4 == 0);
            volatile uint32_t* regBase = (volatile uint32_t*) (reg->addr + reg->offset + offset);
            *regBase = val;
            if (reg->addrB)
            {
                volatile uint32_t* regBaseB =
                    (volatile uint32_t*) (reg->addrB + reg->offset + offset);
                *regBaseB = val;
            }
        }
        else
            assert (0);
    }
    else
        assert (0);
}

// SCI handler
bool PltAcpiSciHandler (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // We only support timer interrupt handling
    uint32_t pm1 = pltAcpiReadReg (ACPI_REG_PM1_STS, 0);
    if (pm1 & ACPI_TMR_STS)
    {
        uint32_t val = pltAcpiReadReg (ACPI_REG_PM_TMR, 0);
        for (int i = 31; i >= 0; --i)
        {
            if (val & (1 << i))
            {
                overFlowTimes = i + 1;
                break;
            }
        }
        if (overFlowTimes == ((is32Bit) ? 32 : 24))
        {
            overFlowSoon = true;
            // Read the timer
            overFlowRead = pltAcpiReadReg (ACPI_REG_PM_TMR, 0);
        }
    }
    pltAcpiWriteReg (ACPI_REG_PM1_STS, ACPI_TMR_STS, 0);
    return true;
}

static ktime_t PltAcpiGetTime()
{
    uint32_t val = pltAcpiReadReg (ACPI_REG_PM_TMR, 0);
    // Check for overflow
    if (overFlowSoon)
    {
        if (val < overFlowRead)
        {
            ++overFlowCount;
            overFlowSoon = false;
        }
    }
    ktime_t mul = 0x1000000;
    if (is32Bit)
        mul = 0x100000000;
    return ((overFlowCount * mul) + val) * acpiPmClock.precision;
}

static void PltAcpiPoll (ktime_t time)
{
    ktime_t target = time + PltAcpiGetTime();
    target /= acpiPmClock.precision;
    while (1)
    {
        uint32_t val = pltAcpiReadReg (ACPI_REG_PM_TMR, 0);
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
    fadt = (AcpiFadt_t*) PltAcpiFindTable ("FACP");
    if (!fadt)
        return NULL;    // Doesn't exist
    if (!fadt->pmTmrLen)
        return NULL;    // ACPI doesn't support PM timer
    if (fadt->flags & ACPI_FADT_TMR_32BIT)
    {
        NkLogDebug ("nexke: using 32 bit ACPI PM\n");
        is32Bit = true;
    }
    acpiPmClock.precision = PLT_NS_IN_SEC / ACPI_PM_FREQ;
    NkLogDebug ("nexke: using ACPI as clock, precision %uns\n", acpiPmClock.precision);
    // Set up ACPI registers
    pltAcpiMapRegs();
    // Set overflow enable only
    pltAcpiWriteReg (ACPI_REG_PM1_EN, ACPI_TMR_EN, 0);
    return &acpiPmClock;
}

// Enable ACPI
void PltAcpiPcEnable()
{
    if (PltGetPlatform()->subType != PLT_PC_SUBTYPE_ACPI)
        return;
    // Get FADT
    fadt = (AcpiFadt_t*) PltAcpiFindTable ("FACP");
    // Check if SCI_EN is set
    if (!(CpuInw (fadt->pm1aCntBlk) & ACPI_SCI_EN))
    {
        CpuOutb (fadt->smiCmd, fadt->acpiEnable);    // Enable ACPI
        while (CpuInw (fadt->pm1aCntBlk) & ACPI_SCI_EN == 0)
            ;
        NkLogDebug ("nexke: enabled ACPI\n");
    }
    else
        NkLogDebug ("nexke: ACPI already enabled\n");
    // Install SCI handler
    if (fadt->sciInt && !NkReadArg ("-nosci"))
    {
        NkHwInterrupt_t* sciInt = PltAllocHwInterrupt();
        sciInt->gsi = PltGetGsi (PLT_BUS_ISA, fadt->sciInt);
        sciInt->mode = PLT_MODE_LEVEL;
        sciInt->flags = PLT_HWINT_ACTIVE_LOW;
        sciInt->handler = PltAcpiSciHandler;
        int vector = PltConnectInterrupt (sciInt);
        if (vector == -1)
            NkPanic ("nexke: unable to install SCI");
        PltInstallInterrupt (vector, sciInt);
    }
}

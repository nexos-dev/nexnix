/*
    mps.c - contains MP spec tables parser
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
#include <nexke/platform/pc.h>
#include <string.h>

// MP tables
typedef struct _mpfloat
{
    uint8_t sig[4];     // "_MP_"
    uint32_t mpConf;    // Physical address of MP configuration table, 0 if non exists
    uint8_t length;     // Length of this table
    uint8_t rev;        // 1 or 4
    uint8_t checksum;
    uint8_t conf;        // Specified default configuration if one exists
    uint8_t features;    // bit 7 = IMCRP
    uint8_t resvd[3];
} __attribute__ ((packed)) PltMpTable_t;

#define PLT_MP_SIG        "_MP_"
#define PLT_MP_FEAT_IMCRP (1 << 7)

// MP configuration table
typedef struct _mpconf
{
    uint8_t sig[4];      // "PCMP"
    uint16_t baseLen;    // Length of base table
    uint8_t rev;         // 1 or 4
    uint8_t checksum;
    uint8_t oemId[8];
    uint8_t productId[12];
    uint32_t oemTable;
    uint16_t oemSize;
    uint16_t entryCount;    // Number of base entries
    uint32_t apicAddr;      // Address of LAPIC
    uint16_t extLen;        // Length of extended table
    uint8_t extChecksum;    // Extended table checksum
    uint8_t resvd;
} __attribute__ ((packed)) PltMpConf_t;

#define PLT_MP_PROCESSOR 0
#define PLT_MP_BUS       1
#define PLT_MP_IOAPIC    2
#define PLT_MP_INT_REDIR 3

// MP table entries

// Processor entry
typedef struct _mpproc
{
    uint8_t type;       // 0
    uint8_t apicId;     // APIC ID of processor
    uint8_t flags;      // Processor flags
    uint32_t family;    // CPU family /stepping/etc
    uint32_t cpuidFlags;
} __attribute__ ((packed)) PltMpProc_t;

#define PLT_MP_PROC_USABLE (1 << 0)
#define PLT_MP_PROC_BSP    (1 << 1)

// Bus entry
typedef struct _mpbus
{
    uint8_t type;       // 1
    uint8_t id;         // ID of bus
    uint8_t name[6];    // String that is bus name
} __attribute__ ((packed)) PltMpBus_t;

// IOAPIC entry
typedef struct _mpapic
{
    uint8_t type;    // 2
    uint8_t id;      // IOAPIC ID
    uint8_t version;
    uint8_t flags;
    uint32_t addr;    // Base address of IOAPIC
} __attribute__ ((packed)) PltMpIoApic_t;

#define PLT_MP_IOAPIC_USABLE (1 << 0)

// Interrupt redirection entry
typedef struct _mpint
{
    uint8_t type;        // 3
    uint8_t intType;     // Interrupt type
    uint16_t intMode;    // Polarity and mode
    uint8_t bus;         // Bus ID
    uint8_t irq;         // Source IRQ on bus
    uint8_t apicId;      // Destination APIC
    uint8_t apicLine;    // APIC interrupt line
} __attribute ((packed)) PltMpInt_t;

#define PLT_MP_PO_MASK 0x3
#define PLT_MP_EL_MASK 0x3

// IMCR ports
#define PLT_IMCR_ADDR   0x22
#define PLT_IMCR_ACCESS 0x70
#define PLT_IMCR_DATA   0x23

#define PLT_IMCR_MASK_EXT 1

// BDA data we need
#define PLT_BDA_EBDA_BASE  0x40E
#define PLT_BDA_BASEMEM_SZ 0x413

static SlabCache_t* cpuCache = NULL;
static SlabCache_t* intCache = NULL;
static SlabCache_t* intCtlCache = NULL;

// Looks for table in specified range
static inline paddr_t pltMpsLook (uint8_t* addr, size_t sz)
{
    for (int i = 0; i < sz; i += sizeof (PltMpTable_t))
    {
        if (!memcmp (addr + i, PLT_MP_SIG, 4))
        {
            // Check checksum now
            if (NkVerifyChecksum (addr + i, sz))
                return (paddr_t) addr + i;
        }
    }
    return 0;
}

// Gets interrupt controller from platform based on ID
static inline PltIntCtrl_t* pltGetIntCtrl (int id)
{
    PltIntCtrl_t* iter = PltGetPlatform()->intCtrls;
    while (iter)
    {
        if (iter->id == id)
            return iter;
        iter = iter->next;
    }
    return NULL;
}

// Detect CPUs, interrupt controllers, interrupts, etc
bool PltMpsDetectCpus()
{
    NexNixBoot_t* boot = NkGetBootArgs();
    // Find MPS component
    if (!(boot->detectedComps & (1 << NB_TABLE_MPS)))
        return false;    // MPS doesn't exist
    assert (boot->comps[NB_TABLE_MPS]);
    // Map table
    PltMpTable_t* mpTable = (PltMpTable_t*) MmAllocKvMmio ((paddr_t) boot->comps[NB_TABLE_MPS],
                                                           2,
                                                           MUL_PAGE_R | MUL_PAGE_KE);
    // Check checksum
    if (!NkVerifyChecksum ((uint8_t*) mpTable, sizeof (PltMpTable_t)))
        return false;
    // Create caches
    cpuCache = MmCacheCreate (sizeof (PltCpu_t), NULL, NULL);
    intCache = MmCacheCreate (sizeof (PltIntOverride_t), NULL, NULL);
    intCtlCache = MmCacheCreate (sizeof (PltIntCtrl_t), NULL, NULL);
    // Check IMCR
    if (mpTable->features & PLT_MP_FEAT_IMCRP)
    {
        // Switch to symmetric mode
        CpuOutb (PLT_IMCR_ADDR, PLT_IMCR_ACCESS);
        CpuOutb (PLT_IMCR_DATA, PLT_IMCR_MASK_EXT);
    }
    if (!mpTable->mpConf)
    {
        // Use a default configuration
        // We don't support all default configurations as of right now
        if (mpTable->conf == 4 || mpTable->conf == 7 || mpTable->conf >= 8)
        {
            NkLogDebug ("nexke: rejected MPS configuration %d\n", mpTable->conf);
            // Don't support MCA
            MmFreeKvMmio (mpTable);
            return false;
        }
        NkLogDebug ("nexke: using MPS configuration %d\n", mpTable->conf);
        // For our purposes all configurations are the same
        // Two CPUs one IOAPIC
        PltCpu_t* cpu1 = (PltCpu_t*) MmCacheAlloc (cpuCache);
        assert (cpu1);
        cpu1->id = 0;
        cpu1->type = PLT_CPU_APIC;
        PltAddCpu (cpu1);
        PltCpu_t* cpu2 = (PltCpu_t*) MmCacheAlloc (cpuCache);
        assert (cpu2);
        cpu2->id = 1;
        cpu1->type = PLT_CPU_APIC;
        PltAddCpu (cpu2);
        PltIntCtrl_t* intCtrl = (PltIntCtrl_t*) MmCacheAlloc (intCtlCache);
        assert (intCtrl);
        intCtrl->addr = PLT_IOAPIC_BASE;
        intCtrl->gsiBase = 0;
        intCtrl->type = PLT_INTCTRL_IOAPIC;
        PltAddIntCtrl (intCtrl);
        // Create override from INT2 to INT0
        PltIntOverride_t* intOv = (PltIntOverride_t*) MmCacheAlloc (intCache);
        intOv->bus = PLT_BUS_ISA;
        intOv->gsi = 2;
        intOv->line = 0;
        intOv->polarity = 0;
        intOv->mode = PLT_MODE_EDGE;
        PltAddInterrupt (intOv);
    }
    else
    {
        // Map table. We don't know the size in advance so we have to map a page
        // get length then map the whole thing
        PltMpConf_t* confTable =
            (PltMpConf_t*) MmAllocKvMmio (mpTable->mpConf, 1, MUL_PAGE_KE | MUL_PAGE_R);
        assert (confTable);
        size_t len = confTable->baseLen;
        MmFreeKvMmio (confTable);
        // Map the whole thing
        confTable = (PltMpConf_t*) MmAllocKvMmio (mpTable->mpConf,
                                                  (CpuPageAlignUp (len) / NEXKE_CPU_PAGESZ) + 1,
                                                  MUL_PAGE_KE | MUL_PAGE_R);
        // Check the checksum
        if (!NkVerifyChecksum ((uint8_t*) confTable, confTable->baseLen))
        {
            NkLogDebug ("nexke: MPS checksum fail\n");
            MmFreeKvMmio (confTable);
            MmFreeKvMmio (mpTable);
            return false;
        }
        // Get to start of entries
        uint8_t* iter = (uint8_t*) (confTable + 1);
        // Loop through it
        uint32_t apicIntBase = 0;
        int isaBusId = 0;
        for (int i = 0; i < confTable->entryCount; ++i)
        {
            if (*iter == PLT_MP_PROCESSOR)
            {
                PltMpProc_t* proc = (PltMpProc_t*) iter;
                PltCpu_t* cpu = (PltCpu_t*) MmCacheAlloc (cpuCache);
                cpu->id = proc->apicId;
                cpu->type = PLT_CPU_APIC;
                PltAddCpu (cpu);
                iter += 20;
            }
            else if (*iter == PLT_MP_BUS)
            {
                PltMpBus_t* bus = (PltMpBus_t*) iter;
                if (!memcmp (bus->name, "ISA   ", 6))
                {
                    isaBusId = bus->id;    // This won't work if the FW puts interrupt assignments
                                           // before the bus, we might should refactor this
                }
                iter += 8;
            }
            else if (*iter == PLT_MP_IOAPIC)
            {
                PltMpIoApic_t* apic = (PltMpIoApic_t*) iter;
                PltIntCtrl_t* intCtrl = (PltIntCtrl_t*) MmCacheAlloc (intCtlCache);
                intCtrl->type = PLT_INTCTRL_IOAPIC;
                intCtrl->addr = apic->addr;
                intCtrl->gsiBase = apicIntBase;
                intCtrl->id = apic->id;
                PltAddIntCtrl (intCtrl);
                // This isn't a very elegant way of doing this, but it works
                apicIntBase += PltApicGetRedirs (intCtrl->addr);
                iter += 8;
            }
            else if (*iter == PLT_MP_INT_REDIR)
            {
                PltMpInt_t* intRedir = (PltMpInt_t*) iter;
                // Determine if this applies to us
                if (intRedir->bus == isaBusId)
                {
                    PltIntOverride_t* hwInt = (PltIntOverride_t*) MmCacheAlloc (intCache);
                    hwInt->bus = PLT_BUS_ISA;
                    // Get the interrupt controller for this override
                    PltIntCtrl_t* ctrl = pltGetIntCtrl (intRedir->apicId);
                    assert (ctrl);
                    hwInt->gsi = ctrl->gsiBase + intRedir->apicLine;
                    hwInt->line = intRedir->irq;
                    // Set trigger mode
                    int mode = (intRedir->type >> 2) & PLT_MP_EL_MASK;
                    if (mode == 0 || mode == 1)
                        hwInt->mode = PLT_MODE_EDGE;
                    else if (mode == 3)
                        hwInt->mode = PLT_MODE_LEVEL;
                    // Set polarity
                    int polarity = intRedir->type & PLT_MP_PO_MASK;
                    if (polarity == 0 || polarity == 1)
                        hwInt->polarity = PLT_POL_ACTIVE_HIGH;
                    else if (mode == 3)
                        hwInt->polarity = PLT_POL_ACTIVE_LOW;
                    PltAddInterrupt (hwInt);
                }
                iter += 8;
            }
            else if (*iter == 4)
            {
                iter += 8;    // Unsupported
            }
            else
                assert (0);
        }
        MmFreeKvMmio (confTable);
    }
    MmFreeKvMmio (mpTable);
    return true;
}

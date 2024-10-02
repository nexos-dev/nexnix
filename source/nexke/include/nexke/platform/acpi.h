/*
    acpi.h - contains nexke ACPI stuff
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

#ifndef _ACPI_H
#define _ACPI_H

#include <nexke/platform.h>
#include <stdbool.h>
#include <stdint.h>

// ACPI tables

// Generic address
typedef struct _gas
{
    uint8_t asId;        // Address space ID
    uint8_t regSz;       // Size of register in bits
    uint8_t regOff;      // Register bit offset
    uint8_t accessSz;    // Size of access
    uint64_t addr;       // Address to be accessed
} __attribute__ ((packed)) AcpiGas_t;

#define ACPI_GAS_MEM 0
#define ACPI_GAS_IO  1

#define ACPI_GAS_SZ_BYTE  1
#define ACPI_GAS_SZ_WORD  2
#define ACPI_GAS_SZ_DWORD 3
#define ACPI_GAS_SZ_QWORD 4

// RSDP
typedef struct _rsdp
{
    uint8_t sig[8];    // "RSD PTR "
    uint8_t checksum;
    uint8_t oemId[6];
    uint8_t rev;          // 1 for ACPI 1, 2 for ACPI 2+
    uint32_t rsdtAddr;    // RSDT address
    // ACPI 2+ fields
    uint32_t length;
    uint64_t xsdtAddr;    // XSDT address
    uint32_t extChecksum;
} __attribute__ ((packed)) AcpiRsdp_t;

// General SDT header
typedef struct _sdt
{
    uint8_t sig[4];     // Signature of table
    uint32_t length;    // Length of table
    uint8_t rev;        // Revision of table
    uint8_t checksum;
    uint8_t oemId[6];
    uint8_t oemTabId[8];
    uint32_t oemRev;
    uint32_t creatorId;
    uint32_t creatorRev;
} __attribute__ ((packed)) AcpiSdt_t;

// FADT table
typedef struct _fadt
{
    AcpiSdt_t sdt;
    uint32_t facs;          // Address of FACS
    uint32_t dsdt;          // Address of DSDT
    uint8_t intModel;       // ACPI 1.0 only
    uint8_t pmProfile;      // Power management profile
    uint16_t sciInt;        // Contains SCI interrupt
    uint32_t smiCmd;        // SMI command port
    uint8_t acpiEnable;     // Value to write to SMI command to enable ACPI
    uint8_t acpiDisable;    // Opposite
    uint8_t s4biosReq;
    uint8_t pstateCnt;
    uint32_t pm1aEvtBlk;    // Hardware model stuff from here on
    uint32_t pm1bEvtBlk;
    uint32_t pm1aCntBlk;
    uint32_t pm1bCntBlk;
    uint32_t pm2CntBlk;
    uint32_t pmTmrBlk;
    uint32_t gpe0Blk;
    uint32_t gpe1Blk;
    uint8_t pm1EvtLen;
    uint8_t pm1CntLen;
    uint8_t pm2CntLen;
    uint8_t pmTmrLen;
    uint8_t gpe0Len;
    uint8_t gpe1Len;
    uint8_t gpe1Base;
    uint8_t cstCnt;
    uint16_t plvl2Lat;
    uint16_t plvl3Lat;
    uint16_t flushSz;
    uint16_t flushStride;
    uint8_t dutyOffset;
    uint8_t dutyWidth;
    uint8_t dayAlarm;
    uint8_t monAlarm;
    uint8_t centReg;
    uint16_t iapcFlags;
    uint8_t resvd;
    uint32_t flags;
    AcpiGas_t resetReg;
    uint8_t resetVal;
    uint8_t resvd1[3];
    uint64_t xFacs;
    uint64_t xDsdt;
    AcpiGas_t xPm1aEvtBlk;    // Extended stuff for ACPI 2.0+
    AcpiGas_t xPm1bEvtBlk;
    AcpiGas_t xPm1aCntBlk;
    AcpiGas_t xPm1bCntBlk;
    AcpiGas_t xPm2CntBlk;
    AcpiGas_t xPmTmrBlk;
    AcpiGas_t xGpe0Blk;
    AcpiGas_t xGpe1Blk;
    AcpiGas_t sleepCtrl;
    AcpiGas_t sleepStatus;
} __attribute__ ((packed)) AcpiFadt_t;

// IA-PC flags
#define ACPI_IAPC_LEGACY_DEVS (1 << 0)
#define ACPI_IAPC_8042_EXISTS (1 << 1)

// Flags
#define ACPI_FADT_TMR_32BIT (1 << 8)
#define ACPI_FADT_HW_REDUCE (1 << 20)

// PM1 event bits
#define ACPI_TMR_EN  (1 << 0)
#define ACPI_TMR_STS (1 << 0)

// PM1 control bits
#define ACPI_SCI_EN  (1 << 0)
#define ACPI_GBL_RLS (1 << 2)

// DBG2 table
typedef struct _dbgdesc
{
    uint8_t rev;
    uint16_t len;
    uint8_t numGases;     // Number of GASes associated with this
    uint16_t nameLen;     // Length of name string
    uint16_t nameOff;     // Name string offset
    uint16_t oemLen;      // Length of OEM data
    uint16_t oemOff;      // OEM data offset
    uint16_t portType;    // Port type and subtype
    uint16_t portSubtype;
    uint16_t resvd;
    uint16_t barOffset;       // Offset to GASes
    uint16_t addrSzOffset;    // Address size offset
} __attribute__ ((packed)) AcpiDbgDesc_t;

#define ACPI_DBG_PORT_SERIAL 0x8000
#define ACPI_DBG_PORT_PL011  3

typedef struct _dbg2
{
    AcpiSdt_t sdt;
    uint32_t devDescOff;    // Device descriptor offset
    uint32_t numDesc;       // Number of device descriptors
} __attribute__ ((packed)) AcpiDbg2_t;

// SPCR table
typedef struct _spcr
{
    AcpiSdt_t sdt;
    uint8_t interface;
    uint8_t resvd[3];
    AcpiGas_t baseAddr;
    uint8_t intType;
    uint8_t irq;
    uint32_t gsi;
    uint8_t baudRate;
    uint8_t parity;
    uint8_t stopBit;
    uint8_t flowControl;
    uint8_t termType;
    uint8_t lang;
    uint16_t pciDev;
    uint16_t pciVendor;
    uint8_t pciLoc[3];
    uint32_t pciFlags;
    uint8_t pciSeg;
    uint32_t uartClock;    // Clock of UART
    uint32_t preciseBaud;
} __attribute__ ((packed)) AcpiSpcr_t;

// ACPI table cache entry
typedef struct _acpicache
{
    struct _acpicache* next;    // Forward pointer
    AcpiSdt_t* table;           // Actual table data, which is after this struct
} AcpiCacheEnt_t;

// Initalizes ACPI
bool PltAcpiInit();

// Finds an ACPI table
AcpiSdt_t* PltAcpiFindTable (const char* sig);

// Initializes ACPI PM timer
PltHwClock_t* PltAcpiInitClock();

#endif

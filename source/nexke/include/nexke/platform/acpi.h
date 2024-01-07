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

// Initalizes ACPI
bool PltAcpiInit();

// Finds an ACPI table
AcpiSdt_t* PltAcpiFindTable (const char* sig);

#endif

/*
    gpt.c - contains functions to build images
    Copyright 2022 The NexNix Project

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

/// @file gpt.c

#include "nnimage.h"
#include <errno.h>
#include <libnex.h>
#include <stdio.h>
#include <string.h>
#include <uuid/uuid.h>

// Sector size in GPT. We default to 512 byte sectors. Newer disks can have 4096 byte sectors, and some SD cards
// have 1024 byte sectors. We let the user override.
#define GPT_DEFAULTSECTSZ 512

// Signature of GPT header
#define GPT_SIGNATURE 0x5452415020494645

// Converts a configuration file size (which is based on a multiplier) to a sector
#define GPT_SZTOSECTOR(sz, mul, sectSz) (((sz) * (mul)) / (sectSz))

// GPT structures

// Main disk header
typedef struct _gptHeader
{
    uint64_t gptSig;            // Signature to identify that this is a GPT
    uint32_t rev;               // Revision of GPT
    uint32_t size;              // Size of this header
    uint32_t headerCrc32;       // CRC32 checksum of this header
    uint32_t resvd;             // Unused
    uint64_t headerLba;         // LBA of this header
    uint64_t otherLba;          // LBA of other header
    uint64_t firstLba;          // First LBA that is usable for partitions on volume
    uint64_t lastLba;           // Last LBA that is usable for partitions on volume
    uuid_t guid;                // Disk GUID
    uint64_t partEntryLba;      // LBA of first partition entry
    uint32_t numParts;          // Number of partitions in array
    uint32_t partEntrySz;       // Size of partition entry
    uint32_t partArrayCrc32;    // CRC32 of all partition entries
} __attribute__ ((packed)) gptHeader_t;

// Partition header
typedef struct _gptPartition
{
    uuid_t typeGuid;      // GUID specifying type
    uuid_t entryGuid;     // GUID for this partition
    uint64_t startLba;    // First LBA sector of partition
    uint64_t endLba;      // Last LBA sector of partition
    uint64_t attr;        // Attributes of partition. Bit 0 indicates if partition is "important",
                          // bits 48 - 63 depend on type GUID
    uint16_t name[36];    // UTF 16 name of string
} __attribute__ ((packed)) gptPartition_t;

// Pointer to main GPT header
static gptHeader_t* mainHeader = NULL;

// Backup header pointer
static gptHeader_t* backupHeader = NULL;

// Pointer to array of partition entries
static gptPartition_t* parts = NULL;

// Current partition being operated on
static int curPart = 0;

// Lowest available sector
static uint64_t nextSector = 0;

// Define recognized type GUIDs
UUID_DEFINE (biosBootGuid,
             0x48,
             0x61,
             0x68,
             0x21,
             0x49,
             0x64,
             0x6F,
             0x6E,
             0x74,
             0x4E,
             0x65,
             0x65,
             0x64,
             0x45,
             0x46,
             0x49);

UUID_DEFINE (espGuid,
             0x28,
             0x73,
             0x2A,
             0xC1,
             0x1F,
             0xF8,
             0xD2,
             0x11,
             0xBA,
             0x4B,
             0x00,
             0xA0,
             0xC9,
             0x3E,
             0xC9,
             0x3B);

UUID_DEFINE (dataGuid,
             0xA2,
             0xA0,
             0xD0,
             0xEB,
             0xE5,
             0xB9,
             0x33,
             0x44,
             0x87,
             0xC0,
             0x68,
             0xB6,
             0xB7,
             0x26,
             0x99,
             0xC7);

bool createGpt (Image_t* img)
{
    // Set sector size if needed
    if (!img->sectSz)
        img->sectSz = GPT_DEFAULTSECTSZ;
    if (!img->bootMode)
        img->bootMode = IMG_BOOTMODE_EFI;
    // Create the protective MBR
    if (!createMbr (img))
        return false;
    if (!addMbrProtectivePartition (img))
        return false;
    // Initialize main header and backup header
    gptHeader_t hdr;
    memset (&hdr, 0, sizeof (gptHeader_t));
    // Set data fields
    hdr.gptSig = EndianChange64 (GPT_SIGNATURE, ENDIAN_LITTLE);
    hdr.rev = EndianChange32 (0x10000, ENDIAN_LITTLE);
    hdr.size = EndianChange32 (sizeof (gptHeader_t), ENDIAN_LITTLE);
    hdr.headerLba = EndianChange64 (1, ENDIAN_LITTLE);
    // Get last sector of disk
    uint64_t lastSector = GPT_SZTOSECTOR (img->sz, img->mul, img->sectSz) - 1;
    // Set backup LBA
    hdr.otherLba = EndianChange64 (lastSector, ENDIAN_LITTLE);
    // Reserve 2 multipliers for partition table structures
    nextSector = GPT_SZTOSECTOR (2, img->mul, img->sectSz);
    hdr.firstLba = EndianChange64 (nextSector, ENDIAN_LITTLE);
    // Last usable LBA is size of disk minus - 2 multipliers - 1
    hdr.lastLba = EndianChange64 (GPT_SZTOSECTOR (img->sz - 2, img->mul, img->sectSz) - 1, ENDIAN_LITTLE);
    // Generate a GUID. Note that this GUID is RNG based, which may not be "unique" per se
    uuid_generate_random (hdr.guid);
    // Ensure partition entries are aligned on multiplier boundary, so we have some manuvering room for excess MBR
    // code
    hdr.partEntryLba = EndianChange64 (GPT_SZTOSECTOR (1, img->mul, img->sectSz), ENDIAN_LITTLE);
    // Number of partitions is based on muliplier size
    hdr.numParts = EndianChange64 (
        ((uint64_t) (GPT_SZTOSECTOR (1, img->mul, img->sectSz) * img->sectSz) / sizeof (gptPartition_t)),
        ENDIAN_LITTLE);
    // Set partition entry size
    hdr.partEntrySz = EndianChange32 (sizeof (gptPartition_t), ENDIAN_LITTLE);
    // Copy this to primary header
    mainHeader = malloc_s (img->sectSz);
    if (!mainHeader)
        return false;
    // Copy contents over
    memcpy (mainHeader, &hdr, sizeof (gptHeader_t));
    // Prepare backup header by editing a couple fields
    // Swap headerLba and otherLba
    uint64_t tmp = hdr.headerLba;
    hdr.headerLba = hdr.otherLba;
    hdr.otherLba = tmp;
    // Set partition table LBA to backup partition table LBA
    hdr.partEntryLba = EndianChange32 (GPT_SZTOSECTOR (img->sz - 2, img->mul, img->sectSz), ENDIAN_LITTLE);
    // Copy to backup header
    backupHeader = malloc_s (img->sectSz);
    if (!backupHeader)
    {
        free (mainHeader);
        return false;
    }
    memcpy (backupHeader, &hdr, sizeof (gptHeader_t));
    // Initialize partition array
    parts = calloc_s (hdr.numParts * sizeof (gptPartition_t));
    if (!parts)
        return false;
    return true;
}

bool addGptPartition (Image_t* img, Partition_t* part)
{
    // Set partition GUID
    uuid_generate_random (parts[curPart].entryGuid);
    // Set partition type GUID
    if (part->isBootPart)
    {
        if (img->bootMode == IMG_BOOTMODE_BIOS)
            memcpy (parts[curPart].typeGuid, biosBootGuid, sizeof (uuid_t));
        else
            memcpy (parts[curPart].typeGuid, espGuid, sizeof (uuid_t));
        BitSet (parts[curPart].attr, 0);
        // If this is a hybrid disk, write out an MBR entry
        if (img->bootMode == IMG_BOOTMODE_HYBRID)
        {
            uint32_t sector = nextSector;
            if (!addMbrPartitionAt (img, part, &sector))
                return false;
        }
    }
    else
        memcpy (parts[curPart].typeGuid, dataGuid, sizeof (uuid_t));
    // Convert name specified to UTF-16
    const char* partName = part->name;
    int i = 0;
    while (i < 36 && *partName)
    {
        UnicodeEncode16 (&parts[curPart].name[i], *partName, ENDIAN_LITTLE);
        ++i;
        ++partName;
    }
    // Check bounds of LBA first
    if (GPT_SZTOSECTOR (part->start, img->mul, img->sectSz) < nextSector)
    {
        error ("partition \"%s\" cannot be placed at specified start location", part->name);
        return false;
    }
    if ((part->start + part->sz) > mainHeader->lastLba)
    {
        error ("partition \"%s\" out of range", part->name);
        return false;
    }
    // Set LBA values
    parts[curPart].startLba = EndianChange64 (GPT_SZTOSECTOR (part->start, img->mul, img->sectSz), ENDIAN_LITTLE);
    parts[curPart].endLba =
        EndianChange64 (GPT_SZTOSECTOR (part->start + part->sz, img->mul, img->sectSz) - 1, ENDIAN_LITTLE);
    ++curPart;
    return true;
}

bool cleanupGpt (Image_t* img)
{
    // Write out partition entries
    uint64_t curPartSector = mainHeader->partEntryLba;
    gptPartition_t* _parts = parts;
    // Write to primary header
    for (int i = 0; i < (mainHeader->numParts * mainHeader->partEntrySz) / img->sectSz; ++i)
    {
        if (!writeSector (img, _parts, curPartSector))
            return false;
        _parts += (img->sectSz / mainHeader->partEntrySz);
        ++curPartSector;
    }
    // Write to backup header
    curPartSector = backupHeader->partEntryLba;
    _parts = parts;
    for (int i = 0; i < (backupHeader->numParts * backupHeader->partEntrySz) / img->sectSz; ++i)
    {
        if (!writeSector (img, _parts, curPartSector))
            return false;
        _parts += (img->sectSz / backupHeader->partEntrySz);
        ++curPartSector;
    }
    // Calculate CRC32 of partition table
    mainHeader->partArrayCrc32 = Crc32Calc ((uint8_t*) parts, mainHeader->numParts * mainHeader->partEntrySz);
    backupHeader->partArrayCrc32 = mainHeader->partArrayCrc32;
    // Calculate CRC32 of partition table header
    mainHeader->headerCrc32 = Crc32Calc ((uint8_t*) mainHeader, sizeof (gptHeader_t));
    backupHeader->headerCrc32 = Crc32Calc ((uint8_t*) backupHeader, sizeof (gptHeader_t));
    // Write out primary header
    if (!writeSector (img, mainHeader, mainHeader->headerLba))
        return false;
    // Write out backup header
    if (!writeSector (img, backupHeader, backupHeader->headerLba))
        return false;
    // Check if
    free (mainHeader);
    free (backupHeader);
    free (parts);
    // Clean up MBR part
    cleanupMbr (img);
    return true;
}

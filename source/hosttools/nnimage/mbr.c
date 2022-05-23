/*
    mbr.c - contains functions to build images
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

/// @file mbr.c

#include "nnimage.h"
#include <errno.h>
#include <libnex.h>
#include <stdio.h>
#include <string.h>

/// MBR partition entry
typedef struct _mbrPart
{
    uint8_t attr;           ///< Bit 7 = bootable flag
    uint8_t chsStart[3];    ///< Obsolete
    uint8_t type;           ///< Partition type
    uint8_t chsEnd[3];      ///< Obsolete
    uint32_t lbaStart;      ///< LBA start of partition
    uint32_t lbaSz;         ///< Number of sectors in partition
} __attribute__ ((packed)) mbrPart_t;

/// MBR format on disk
typedef struct _mbr
{
    char bootstrap[440];    ///< Bootstrap program
    uint32_t unused;        ///< Disk ID. Unused here
    uint16_t unused2;       ///< Unused here as well
    mbrPart_t parts[4];     ///< Partitions on disk
    uint16_t bootSig;       ///< 0xAA55 signature
} __attribute__ ((packed)) mbr_t;

// Table of partition types
static uint8_t mbrTypesTable[] = {
    0xFF,    // Unsupported
    0x0C,    // FAT32
    0x0E,    // FAT16
    0x06,    // FAT12
    0x83,    // Ext2
    0xFF     // ISO9660 is unsupported on MBR (obviously)
};

// Size of sector in MBR. We ignore user specified overrides
#define MBR_SECTORSZ 512

// Converts a configuration file size (which is based on a multiplier) to a sector
#define MBR_SZTOSECTOR(sz, mul, sectSz) (((sz) * (mul)) / (sectSz))

/// The number of partitions left on the current MBR
static int partsLeft = 0;

/// The current MBR
static mbr_t* curMbr = NULL;

// Sector on which above MBR resides
static uint64_t curMbrSector = 0;

// The next available sector
static uint32_t nextSector = 0;

// Current partition index in MBR
static uint8_t mbrIdx = 0;

// Start of extended partition
static uint32_t extPartStart = 0;

// Start of currrent logical partition
static uint32_t logicalPartStart = 0;

bool createMbr (Image_t* img)
{
    // Set sector size in img
    if (!img->sectSz)
        img->sectSz = MBR_SECTORSZ;
    // Create the mbr
    curMbr = (mbr_t*) malloc_s (sizeof (mbr_t));
    if (!curMbr)
        return false;
    memset (curMbr, 0, sizeof (mbr_t));
    // Set boot signature
    curMbr->bootSig = EndianChange16 (0xAA55, ENDIAN_LITTLE);
    // Make sure sector pointer is right
    ++nextSector;
    // Figure out the number of available partitions on this MBR.
    // If there are 4 or less (e.g., no extended MBR) there are 4. Else, 3
    if (img->partCount > 4)
        partsLeft = 3;
    else
        partsLeft = 4;
    // Write it out
    if (!writeSector (img, curMbr, 0))
    {
        free (curMbr);
        return false;
    }
    else
        return true;
}

void mountMbrPartition (Image_t* img, Partition_t* part)
{
    part->internal.lbaStart = MBR_SZTOSECTOR (part->start, img->mul, img->sectSz);
    part->internal.lbaSz = MBR_SZTOSECTOR (part->sz, img->mul, img->sectSz);
}

bool addMbrPartition (Image_t* img, Partition_t* part)
{
    return addMbrPartitionAt (img, part, &nextSector);
}

bool addMbrPartitionAt (Image_t* img, Partition_t* part, uint32_t* _nextSector)
{
    // This is kind of a nightmare....
    // This code supports extended MBRs. Basically, in a nutshell, heres the nomenclature:
    // a primary partition is any of the first three
    // partitions on disk. The extended partition is specified in the fourth entry of the primary MBR.
    // Its size spans the rest of the disk. The lbaStart of it points to the first extended boot record (EBR)
    // The EBR has one entry that specifies a logical partition (which is any partition other than a
    // primary or the extended one). The next entry contains a the lbaStart of the next EBR, and its size
    // is the next EBR, plus padding, plus the logical partition contained in said next EBR
    // Adapted from: https://en.wikipedia.org/wiki/Extended_boot_record (as no MBR / EBR spec exists!)

    // Check if a new EBR should be created, or if the extended partition should be created
    if (partsLeft <= 0)
    {
        // Add extended MBR to next partition table entry
        curMbr->parts[mbrIdx].type = 0x0F;    // LBA logical partition
        curMbr->parts[mbrIdx].lbaStart = EndianChange32 ((*_nextSector) - extPartStart, ENDIAN_LITTLE);
        // Size of extended partition container will be size of the rest of the disk,
        // size of logical partition container is size of partition contained plus the EBR plus any padding
        if (extPartStart)
            curMbr->parts[mbrIdx].lbaSz =
                EndianChange32 (MBR_SZTOSECTOR (part->sz + img->mul, img->mul, img->sectSz), ENDIAN_LITTLE);
        else
            curMbr->parts[mbrIdx].lbaSz =
                EndianChange32 (MBR_SZTOSECTOR (img->sz, img->mul, img->sectSz) - (*_nextSector), ENDIAN_LITTLE);
        // Write out old MBR
        if (!writeSector (img, curMbr, curMbrSector))
            return false;
        // Set up new EBR
        memset (curMbr, 0, sizeof (mbr_t));
        curMbr->bootSig = EndianChange16 (0xAA55, ENDIAN_LITTLE);
        // Write out new EBR
        curMbrSector = *_nextSector;
        if (!writeSector (img, curMbr, curMbrSector))
            return false;
        *_nextSector += 1;
        // Figure out if extended partition start should be set
        if (!extPartStart)
            extPartStart = curMbrSector;
        logicalPartStart = curMbrSector;
        partsLeft = 1;
        mbrIdx = 0;
    }
    // Check if this partition is in a valid spot
    if (MBR_SZTOSECTOR (part->start, img->mul, img->sectSz) < *_nextSector)
    {
        error ("partition \"%s\" cannot be placed at specified start location", part->name);
        return false;
    }
    // Check if partition size is past image size
    if ((part->start + part->sz) > img->sz)
    {
        error ("partition \"%s\" out of range", part->name);
        return false;
    }
    // Add new partition
    curMbr->parts[mbrIdx].attr = part->isBootPart ? 0x80 : 0;
    curMbr->parts[mbrIdx].lbaStart =
        EndianChange32 (MBR_SZTOSECTOR (part->start, img->mul, img->sectSz) - logicalPartStart, ENDIAN_LITTLE);
    curMbr->parts[mbrIdx].lbaSz = EndianChange32 (MBR_SZTOSECTOR (part->sz, img->mul, img->sectSz), ENDIAN_LITTLE);
    // Check type
    if (mbrTypesTable[part->filesys] == 0xFF)
    {
        error ("unsupported filesystem \"%s\" on partition table format \"%s\"",
               fsTypeNames[part->filesys],
               partTypeNames[img->format]);
        return false;
    }
    curMbr->parts[mbrIdx].type = mbrTypesTable[part->filesys];
    // Set partition internal info
    part->internal.lbaStart = MBR_SZTOSECTOR (part->start, img->mul, img->sectSz);
    part->internal.lbaSz = MBR_SZTOSECTOR (part->sz, img->mul, img->sectSz);
    ++mbrIdx;
    --partsLeft;
    *_nextSector += MBR_SZTOSECTOR (part->sz, img->mul, img->sectSz);
    return true;
}

bool addMbrProtectivePartition (Image_t* img)
{
    curMbr->parts[mbrIdx].attr = 0;
    curMbr->parts[mbrIdx].lbaStart = EndianChange32 (1, ENDIAN_LITTLE);
    curMbr->parts[mbrIdx].lbaSz =
        EndianChange32 (MBR_SZTOSECTOR (img->sz, img->mul, img->sectSz) - 1, ENDIAN_LITTLE);
    curMbr->parts[mbrIdx].type = 0xEE;
    // Write out MBR
    if (!writeSector (img, curMbr, curMbrSector))
        return false;
    ++mbrIdx;
    --partsLeft;
    return true;
}

bool cleanupMbr (Image_t* img)
{
    // Write out MBR
    if (!writeSector (img, curMbr, curMbrSector))
        return false;

    // Free MBR
    free (curMbr);
    return true;
}

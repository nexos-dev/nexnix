/*
    volmanager.c - contains volume manager
    Copyright 2023 The NexNix Project

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
#include <libnex/crc32.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/disk.h>
#include <nexboot/drivers/volume.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdio.h>
#include <string.h>

extern NbObjSvcTab_t volManagerSvcTab;
extern NbDriver_t volManagerDrv;

// MBR defines
#define MBR_SIGNATURE   0xAA55
#define MBR_FLAG_ACTIVE (1 << 7)
#define MBR_MAX_PARTS   4

// MBR filesystems
#define MBR_FS_FAT12       0x01
#define MBR_FS_SMALL_FAT16 0x04
#define MBR_EXTPART_CHS    0x05
#define MBR_FS_FAT16       0x06
#define MBR_FS_FAT32       0x0B
#define MBR_FS_FAT32_LBA   0x0C
#define MBR_FS_FAT16_LBA   0x0E
#define MBR_EXTPART_LBA    0x0F
#define MBR_FS_EXT2        0x83
#define MBR_GPT_PART       0xEE

// MBR partition
typedef struct _mbrPart
{
    uint8_t flags;
    uint8_t chsStart[3];
    uint8_t type;    // Partition filesystem
    uint8_t chsEnd[3];
    uint32_t lbaStart;
    uint32_t partSz;    // Size in sectors
} __attribute__ ((packed)) MbrPart_t;

// MBR partition table
typedef struct _mbr
{
    uint8_t bootstrap[440];    // Bootstrap code
    uint32_t sig;
    uint16_t resvd;
    MbrPart_t parts[MBR_MAX_PARTS];
    uint16_t bootSig;
} __attribute__ ((packed)) Mbr_t;

// GPT structures
typedef struct _gpt
{
    uint8_t sig[8];       // "EFI PART"
    uint32_t rev;         // GPT revision
    uint32_t hdrSize;     // GPT header size
    uint32_t hdrCrc32;    // Header checksum
    uint32_t resvd;
    uint64_t hdrLba;            // LBA of this header
    uint64_t altHdrLba;         // LBA of backup header
    uint64_t firstDataLba;      // LBA of start of data region
    uint64_t lastDataLba;       // LBA of end of data region
    uint8_t diskGuid[16];       // GUID of disk
    uint64_t partTableLba;      // LBA of start of partition table
    uint32_t numParts;          // Number of partitions
    uint32_t partEntSize;       // Size of partition entry
    uint32_t partEntriesCrc;    // CRC32 of partition array
} __attribute__ ((packed)) Gpt_t;

typedef struct _gptPart
{
    uint8_t partTypeGuid[16];    // GUID of partition type
    uint8_t partGuid[16];        // GUID of partition
    uint64_t startLba;           // Start LBA of partition
    uint64_t endLba;             // End LBA of partition
    uint64_t attr;               // Attributes of partition
    uint8_t name[72];            // Name of partition
} __attribute__ ((packed)) GptPart_t;

// clang-format off

// Partition type GUIDs
static uint8_t espGuid[] = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                            0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};

static uint8_t bdpGuid[] = {0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33,
                            0x44, 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7};

static uint8_t ext2Guid[] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                           0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};

static uint8_t bbpGuid[] = {0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6f, 0x6e,
                            0x74, 0x4e, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49};

// clang-format on

// ISO9660 stuff
#define ISO9660_VOLUME_DESC_START 0x10

static int curDisk = 0;    // Current disk being initialized
static int curPart = 0;    // Current partition being initialized

// Adds a volume to object database
static void addVolume (NbVolume_t* vol)
{
    NbLogMessageEarly ("volmanager: Found volume %d on disk %d\r\n",
                       NEXBOOT_LOGLEVEL_DEBUG,
                       curPart,
                       curDisk);
    // Create object
    char buf[64] = {0};
    snprintf (buf, 64, "/Volumes/Disk%d/Volume%d", curDisk, curPart);
    NbObject_t* obj = NbObjCreate (buf, OBJ_TYPE_DEVICE, OBJ_INTERFACE_VOLUME);
    NbObjSetData (obj, vol);
    NbObjInstallSvcs (obj, &volManagerSvcTab);
    ++curPart;    // Update curPart
}

// Converts MBR partition type to volume type
static int mbrTypeToFs (uint8_t mbrType)
{
    switch (mbrType)
    {
        case MBR_FS_FAT12:
            return VOLUME_FS_FAT12;
        case MBR_FS_FAT16:
        case MBR_FS_FAT16_LBA:
        case MBR_FS_SMALL_FAT16:
            return VOLUME_FS_FAT16;
        case MBR_FS_FAT32:
        case MBR_FS_FAT32_LBA:
            return VOLUME_FS_FAT32;
        case MBR_FS_EXT2:
            return VOLUME_FS_EXT2;
        default:
            return 0;
    }
}

// Checks if partition type is active
static bool gptIsActive (uint8_t* type)
{
    if (!memcmp (type, bbpGuid, 16))
        return true;
    else if (!memcmp (type, espGuid, 16))
        return true;
    else
        return false;
}

// Gets file system code from GUID
static uint32_t gptTypeToFs (uint8_t* type)
{
    if (!memcmp (type, bdpGuid, 16) || !memcmp (type, bbpGuid, 16) ||
        !memcmp (type, espGuid, 16))
        return VOLUME_FS_FAT;
    else if (!memcmp (type, ext2Guid, 16))
        return VOLUME_FS_EXT2;
    else
        return 0;
}

// Reads GPT table
void readGpt (NbObject_t* diskObj)
{
    NbDiskInfo_t* disk = (NbDiskInfo_t*) NbObjGetData (diskObj);
    // Allocate GPT header
    Gpt_t* gpt = (Gpt_t*) malloc (disk->sectorSz);
    assert (gpt);
    // Read it in
    NbReadSector_t sector;
    sector.buf = gpt;
    sector.count = 1;
    sector.sector = 1;
    if (!NbObjCallSvc (diskObj, NB_DISK_READ_SECTORS, &sector))
    {
        // Report error
        NbObjCallSvc (diskObj, NB_DISK_REPORT_ERROR, (void*) sector.error);
        NbCrash();
    }
    // Validate CRC32
    uint32_t gptCrc = gpt->hdrCrc32;
    gpt->hdrCrc32 = 0;
    uint32_t crc32 = Crc32Calc ((uint8_t*) gpt, gpt->hdrSize);
    if (gptCrc != crc32 || gpt->hdrLba != 1 || memcmp (gpt->sig, "EFI PART", 8))
    {
        NbLogMessageEarly ("volmanager: GPT corrupt on %s\r\n",
                           NEXBOOT_LOGLEVEL_EMERGENCY,
                           diskObj->name);
        NbCrash();
    }
    // Begin parsing the partitions
    GptPart_t* part = (GptPart_t*) malloc (disk->sectorSz);
    GptPart_t* opart = part;
    assert (part);
    curPart = 0;
    int curSector = 0;
    do
    {
        // Read in the sector
        sector.buf = part;
        sector.count = 1;
        sector.sector = gpt->partTableLba + curSector;
        if (!NbObjCallSvc (diskObj, NB_DISK_READ_SECTORS, &sector))
        {
            // Report error
            NbObjCallSvc (diskObj, NB_DISK_REPORT_ERROR, (void*) sector.error);
            NbCrash();
        }
        // Loop through each entry
        for (int i = 0; i < (disk->sectorSz / gpt->partEntSize); ++i, ++part)
        {
            if (!part->startLba)
                break;    // End of table
            NbVolume_t* vol = (NbVolume_t*) malloc (sizeof (NbVolume_t));
            assert (vol);
            // Set fields
            vol->disk = NbObjRef (diskObj);
            vol->isActive = gptIsActive (part->partTypeGuid);
            vol->isPartition = true;
            vol->volSize = part->endLba - part->startLba;
            vol->volStart = part->startLba;
            vol->volFileSys = gptTypeToFs (part->partTypeGuid);
            // Add volume to object database
            addVolume (vol);
        }
        ++curSector;
    } while (part->startLba);    // Go until entry is zeroed
    free (opart);
    free (gpt);
}

// Parses and MBR table
void readMbr (NbObject_t* diskObj, uint8_t* sector)
{
    Mbr_t* mbr = (Mbr_t*) sector;
    assert (mbr->bootSig == 0xAA55);
    // Check media byte to see if this is a floppy
    if (sector[0x15] == 0xF9 || sector[0x15] == 0xF0)
    {
        NbVolume_t* vol = (NbVolume_t*) malloc (sizeof (NbVolume_t));
        NbDiskInfo_t* diskInf = NbObjGetData (diskObj);
        assert (vol);
        vol->disk = NbObjRef (diskObj);
        vol->isActive = true;
        vol->isPartition = false;
        vol->volFileSys = VOLUME_FS_FAT12;
        vol->volStart = 0;
        vol->volSize = diskInf->size;
        addVolume (vol);
    }
    for (int i = 0; i < MBR_MAX_PARTS; ++i)
    {
        if (mbr->parts[i].type)
        {
            if (!mbrTypeToFs (mbr->parts[i].type))
            {
                if (mbr->parts[i].type == MBR_GPT_PART)
                    readGpt (diskObj);
                continue;
            }
            NbVolume_t* vol = (NbVolume_t*) malloc (sizeof (NbVolume_t));
            assert (vol);
            // Set fields
            vol->disk = NbObjRef (diskObj);
            vol->isActive = mbr->parts[i].flags;
            vol->isPartition = true;
            vol->volSize = mbr->parts[i].partSz;
            vol->volStart = mbr->parts[i].lbaStart;
            vol->volFileSys = mbrTypeToFs (mbr->parts[i].type);
            // Add volume to object database
            addVolume (vol);
        }
    }
}

// Parses partition table
static bool readPartitionTable (NbObject_t* diskObj)
{
    // Reset curPart
    curPart = 0;
    NbDiskInfo_t* disk = (NbDiskInfo_t*) NbObjGetData (diskObj);
    // Read sector 0
    uint8_t* sector0 = malloc (disk->sectorSz);
    assert (sector0);
    NbReadSector_t sector;
    sector.buf = sector0;
    sector.count = 1;
    sector.sector = 0;
    if (!NbObjCallSvc (diskObj, NB_DISK_READ_SECTORS, &sector))
    {
        // Report error
        NbObjCallSvc (diskObj, NB_DISK_REPORT_ERROR, (void*) sector.error);
        NbCrash();
    }
    // Check if this is a MBR
    uint16_t* sig = (uint16_t*) (sector0 + 0x1FE);
    if (*sig == 0xAA55)
        readMbr (diskObj, sector0);
    else
    {
        // For now, only other option is that this a no emulation ISO9660. Check to
        // be sure
        NbReadSector_t sector;
        sector.buf = sector0;
        sector.count = 1;
        sector.sector = ISO9660_VOLUME_DESC_START;
        if (!NbObjCallSvc (diskObj, NB_DISK_READ_SECTORS, &sector))
        {
            // Report error
            NbObjCallSvc (diskObj, NB_DISK_REPORT_ERROR, (void*) sector.error);
            NbCrash();
        }
        // Check signature
        if (!memcmp (sector0 + 1, "CD001", 5))
        {
            // Create volume
            // NOTE: we dont attempt to read PVD to set fields in volume.
            // The volume covers the entire CD-ROM
            // Not sure if this is the best way, however
            NbVolume_t* vol = (NbVolume_t*) malloc (sizeof (NbVolume_t));
            assert (vol);
            // Set fields
            vol->disk = NbObjRef (diskObj);
            vol->isActive = true;
            vol->isPartition = false;
            vol->volSize = disk->size;
            vol->volStart = 0;
            vol->volFileSys = VOLUME_FS_ISO9660;
            // Add volume to object database
            addVolume (vol);
        }
    }
    free (sector0);
    return true;
}

static bool VolManagerEntry (int code, void* params)
{
    switch (code)
    {
        case VOLUME_ADD_DISK: {
            NbDiskInfo_t* disk = (NbDiskInfo_t*) (((NbObject_t*) params)->data);
            // Create directory for disk
            char buf[64] = {0};
            snprintf (buf, 64, "/Volumes/Disk%d", curDisk);
            NbObjCreate (buf, OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
            // Read partition table
            readPartitionTable ((NbObject_t*) params);
            ++curDisk;
            break;
        }
    }
    return true;
}

static bool VolManagerDumpData (void* objp, void* params)
{
    return true;
}

static bool VolManagerNotify (void* objp, void* params)
{
    return true;
}

static bool VolManagerReadBlocks (void* objp, void* params)
{
    NbObject_t* volObj = objp;
    NbVolume_t* vol = NbObjGetData (volObj);
    NbReadBlock_t* block = params;
    // Determine real sector base
    block->sector += vol->volStart;
    // Do a bounds check
    if ((block->sector + block->count) > (vol->volStart + vol->volSize))
        return false;
    bool res = NbObjCallSvc (vol->disk, NB_DISK_READ_SECTORS, block);
    block->sector -= vol->volStart;    // Undo change we made
    return res;
}

static NbObjSvc volManagerSvcs[] =
    {NULL, NULL, NULL, VolManagerDumpData, VolManagerNotify, VolManagerReadBlocks};

NbObjSvcTab_t volManagerSvcTab = {ARRAY_SIZE (volManagerSvcs), volManagerSvcs};

NbDriver_t volManagerDrv = {"VolManager", VolManagerEntry, {0}, 0, false, 0};

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

// Parses and MBR table
void readMbr (NbObject_t* diskObj, uint8_t* sector)
{
    Mbr_t* mbr = (Mbr_t*) sector;
    assert (mbr->bootSig == 0xAA55);
    for (int i = 0; i < MBR_MAX_PARTS; ++i)
    {
        if (mbr->parts[i].type)
        {
            if (!mbrTypeToFs (mbr->parts[i].type))
                continue;    // Partition unrecognized
            NbVolume_t* vol = (NbVolume_t*) malloc (sizeof (NbVolume_t));
            assert (vol);
            // Set fields
            vol->disk = diskObj;
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
    free (sector0);
    return true;
}

static bool VolManagerEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_START:
            NbObjCreate ("/Volumes", OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
            break;
        case VOLUME_ADD_DISK: {
            NbDiskInfo_t* disk = (NbDiskInfo_t*) (((NbObject_t*) params)->data);
            // Create directory for disk
            char buf[64] = {0};
            snprintf (buf, 64, "/Volumes/Disk%d", curDisk);
            NbObjCreate (buf, OBJ_TYPE_DIR, OBJ_INTERFACE_DIR);
            // First attempt to read partition table
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

static NbObjSvc volManagerSvcs[] = {NULL,
                                    NULL,
                                    NULL,
                                    VolManagerDumpData,
                                    VolManagerNotify};

NbObjSvcTab_t volManagerSvcTab = {ARRAY_SIZE (volManagerSvcs), volManagerSvcs};

NbDriver_t volManagerDrv = {"VolManager", VolManagerEntry, {0}, 0, false, 0};

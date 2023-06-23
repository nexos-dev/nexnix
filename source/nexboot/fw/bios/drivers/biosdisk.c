/*
    biosdisk.c - contains BIOS disk driver
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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <stdio.h>
#include <string.h>

typedef struct _biosDisk
{
    NbHwDevice_t hdr;     // Standard header
    uint8_t biosNum;      // BIOS disk number of this drive
    uint8_t flags;        // Disk flags
    int type;             // Media type of disk
    uint64_t size;        // Size of disk in sectors
    uint16_t sectorSz;    // Size of one sector
    uint16_t hpc;         // Heads per cylinder
    uint8_t spt;          // Sectors per track
} NbBiosDisk_t;

// Drive parameter table from BIOS
typedef struct _dpt
{
    uint16_t sz;       // Size of this structure
    uint16_t flags;    // Flags of this drive
    uint32_t defCyls;
    uint32_t defHeads;
    uint32_t defSpt;
    uint64_t diskSz;            // Size of disk in sectors
    uint16_t bytesPerSector;    // Bytes in a sector
} __attribute__ ((packed)) NbDriveParamTab_t;

// BIOS DAP
typedef struct _dap
{
    uint8_t sz;    // Size of DAP
    uint8_t resvd;
    uint8_t count;    // Sectors to read
    uint8_t resvd1;
    uint16_t bufOffset;    // Offset to read to
    uint16_t bufSeg;       // Segment to read to
    uint64_t sector;       // Sector to start reading from
} __attribute__ ((packed)) NbBiosDap_t;

// DPT flags
#define DPT_FLAG_DMA_ERR_TRANSPARENT  (1 << 0)
#define DPT_FLAG_GEOMETRY_VALID       (1 << 1)
#define DPT_FLAG_MEDIA_REMOVABLE      (1 << 2)
#define DPT_FLAG_WRITE_VERIFY         (1 << 3)
#define DPT_FLAG_MEDIA_CHANGE_SUPPORT (1 << 4)
#define DPT_FLAG_MEDIA_LOCKABLE       (1 << 5)
#define DPT_FLAG_NO_MEDIA             (1 << 6)

// Disk errors
#define DISK_ERROR_NOERROR           0
#define DISK_ERROR_INVALID_CMD       1
#define DISK_ERROR_NOADDR_MARK       2
#define DISK_ERROR_WRITE_PROTECT     3
#define DISK_ERROR_NO_SECTOR         4
#define DISK_ERROR_RESET_FAILED      5
#define DISK_ERROR_REMOVED           6
#define DISK_ERROR_BADTABLE          7
#define DISK_ERROR_DMA_OVERRUN       8
#define DISK_ERROR_DMA_CROSS         9
#define DISK_ERROR_BAD_SECTOR        10
#define DISK_ERROR_BAD_HEAD          11
#define DISK_ERROR_BAD_MEDIA         12
#define DISK_ERROR_INVALID_SECTORS   13
#define DISK_ERROR_MARK_FOUND        14
#define DISK_ERROR_DMA_ARBIT_FAILED  15
#define DISK_ERROR_CHECKSUM_ERROR    16
#define DISK_ERROR_CONTROLLER_FAILED 32
#define DISK_ERROR_SEEK_FAILED       64
#define DISK_ERROR_TIMEOUT           128
#define DISK_ERROR_NOT_READY         0xAA
#define DISK_ERROR_UNDEFINED         0xBB
#define DISK_ERROR_WRITE_FAIL        0xCC

// BIOS disk functions
#define BIOS_DISK_CHECK_LBA         0x41
#define BIOS_LBA_INTERFACE_EJECTING (1 << 1)
#define BIOS_LBA_INTERFACE_64BIT    (1 << 3)

#define BIOS_DISK_GET_DPT    0x48
#define BIOS_DISK_GET_TYPE   0x15
#define BIOS_DISK_GET_PARAMS 0x08

// Error to string table
const char* diskErrorStrs[] = {"No error",
                               "Invalid disk command",
                               "No address mark found",
                               "Disk write protected",
                               "Sector not found",
                               "Reset failed",
                               "Disk removed",
                               "Bad table",
                               "DMA overrun",
                               "DMA crossed boundary",
                               "Bad sector",
                               "Bad head",
                               "Bad media type",
                               "Invalid sectors",
                               "Mark found",
                               "DMA arbitration failed",
                               "Checksum error",
                               "Controller failed",
                               "Seek failed",
                               "Disk timeout",
                               "Disk not ready",
                               "Undefined error",
                               "Disk write failed"};

extern NbObjSvcTab_t biosDiskSvcTab;

static uint8_t curDisk = 0;    // Current disk being checked

static bool BiosDiskEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {
            NbBiosDisk_t* disk = params;
            memset (disk, 0, sizeof (NbBiosDisk_t));
            disk->hdr.devId = curDisk;
            disk->hdr.devSubType = 0;
            NbBiosRegs_t in = {0}, out = {0};
        checkDisk : {
            // To circumvent some buggy BIOSes, we read from the disk and check for
            // success to see if the disk exists or not
            bool diskSuccess = false;
            do
            {
                NbLogMessageEarly ("biosdisk: Checking BIOS disk %#X\r\n",
                                   NEXBOOT_LOGLEVEL_DEBUG,
                                   curDisk);
                // Read in a sector from the disk, selecting the right function
                if (curDisk < 0x80)
                {
                    memset (&in, 0, sizeof (NbBiosRegs_t));
                    in.ah = 0x02;
                    in.al = 1;
                    in.cl = 1;
                    in.dl = curDisk;
                    in.es = NEXBOOT_BIOSBUF_BASE >> 4;
                    in.di = NEXBOOT_BIOSBUF_BASE & 0xF;
                    NbBiosCall (0x13, &in, &out);
                    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                    {
                        NbLogMessageEarly (
                            "biosdisk: BIOS disk %#X doesn't exist\r\n",
                            NEXBOOT_LOGLEVEL_DEBUG,
                            curDisk);
                        // No disks left to check, move to hard disks
                        curDisk = 0x80;
                    }
                    else
                        diskSuccess = true;    // We found a disk
                }
                else
                {
                    // Read using LBA
                    NbBiosDap_t* dap = (NbBiosDap_t*) NEXBOOT_BIOSBUF2_BASE;
                    dap->sz = 16;
                    dap->bufOffset = NEXBOOT_BIOSBUF_BASE & 0xF;
                    dap->bufSeg = (NEXBOOT_BIOSBUF_BASE + 64) >> 4;
                    dap->count = 1;
                    dap->sector = 5;
                    memset (&in, 0, sizeof (NbBiosRegs_t));
                    in.ah = 0x42;
                    in.dl = curDisk;
                    in.ds = NEXBOOT_BIOSBUF2_BASE >> 4;
                    in.si = NEXBOOT_BIOSBUF2_BASE & 0xF;
                    NbBiosCall (0x13, &in, &out);
                    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                    {
                        NbLogMessageEarly (
                            "biosdisk: BIOS disk %#X doesn't exist\r\n",
                            NEXBOOT_LOGLEVEL_DEBUG,
                            curDisk);
                        if (curDisk < 0x81)
                            curDisk = 0x81;    // Move to CD-ROMs
                        // Check if there are any left to check
                        else if (curDisk >= 0x8A)
                            return false;
                    }
                    else
                        diskSuccess = true;    // We found a disk
                }
            } while (!diskSuccess);
            disk->biosNum = curDisk;
        }
            // Check for LBA extensions on current device
            memset (&in, 0, sizeof (NbBiosRegs_t));
            memset (&out, 0, sizeof (NbBiosRegs_t));
            in.ah = BIOS_DISK_CHECK_LBA;
            in.bx = 0x55AA;
            in.dl = curDisk;
            NbBiosCall (0x13, &in, &out);
            if (out.flags & NEXBOOT_CPU_CARRY_FLAG || out.bx != 0xAA55)
            {
                assert (curDisk <= 0x8A);
                // Check using get disk type function
                memset (&in, 0, sizeof (NbBiosRegs_t));
                memset (&out, 0, sizeof (NbBiosRegs_t));
                if (curDisk >= 0x80)
                {
                    in.ah = BIOS_DISK_GET_TYPE;
                    in.al = 0xFF;
                    in.dl = curDisk;
                    NbBiosCall (0x13, &in, &out);
                    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                    {
                        ++curDisk;
                        goto checkDisk;
                    }
                    if (out.ah == 0)
                        disk->flags |= DISK_FLAG_REMOVABLE;    // Disk is removable
                    disk->size = out.dx;
                    disk->size |= (out.cx << 16);
                }
                else
                    disk->flags |= DISK_FLAG_REMOVABLE;
                disk->sectorSz = 512;
                memset (&in, 0, sizeof (NbBiosRegs_t));
                memset (&out, 0, sizeof (NbBiosRegs_t));
                // Get geometry
                in.ah = BIOS_DISK_GET_PARAMS;
                in.dl = curDisk;
                in.es = 0;
                in.di = 0;
                NbBiosCall (0x13, &in, &out);
                if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                {
                    ++curDisk;
                    goto checkDisk;
                }
                disk->hpc = out.dh + 1;
                disk->spt = out.cl & 0x3F;
                uint32_t numCyls = ((out.cl << 8) || out.ch) + 1;
                if (curDisk < 0x80)
                {
                    disk->size = (uint64_t) (disk->spt * disk->hpc * numCyls);
                }
                NbLogMessageEarly ("biosdisk: Found disk %#X with size %u, sector "
                                   "size %d, flags %d, HPC %d, SPT %d\r\n",
                                   NEXBOOT_LOGLEVEL_DEBUG,
                                   curDisk,
                                   disk->size,
                                   disk->sectorSz,
                                   disk->flags,
                                   disk->hpc,
                                   disk->spt);
            }
            else
            {
                NbLogMessageEarly ("biosdisk: Disk supports LBA extensions\r\n",
                                   NEXBOOT_LOGLEVEL_DEBUG);
                disk->flags |= DISK_FLAG_LBA;
                // Check interfaces
                if (out.cx & BIOS_LBA_INTERFACE_EJECTING)
                    disk->flags |= DISK_FLAG_EJECTABLE;
                if (out.cx & BIOS_LBA_INTERFACE_64BIT)
                    disk->flags |= DISK_FLAG_64BIT;
                // Get DPT
                NbDriveParamTab_t* dpt = (NbDriveParamTab_t*) NEXBOOT_BIOSBUF_BASE;
                dpt->sz = sizeof (NbDriveParamTab_t);
                memset (&in, 0, sizeof (NbBiosRegs_t));
                memset (&out, 0, sizeof (NbBiosRegs_t));
                in.ah = BIOS_DISK_GET_DPT;
                in.dl = curDisk;
                in.si = ((uint32_t) dpt & 0x0F);
                in.ds = ((uint32_t) dpt >> 4);
                NbBiosCall (0x13, &in, &out);
                if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                {
                    NbLogMessageEarly ("biosdisk: Disk %#X not working\r\n",
                                       NEXBOOT_LOGLEVEL_DEBUG,
                                       curDisk);
                    ++curDisk;
                    goto checkDisk;    // Retry
                }
                assert (dpt->sz == sizeof (NbDriveParamTab_t));
                // Set fields
                disk->sectorSz = dpt->bytesPerSector;
                disk->size = dpt->diskSz;
                if (dpt->flags & DPT_FLAG_MEDIA_REMOVABLE)
                {
                    // Check if there is media
                    if (dpt->flags & DPT_FLAG_NO_MEDIA)
                        return false;    // No media
                    disk->flags |= DISK_FLAG_REMOVABLE;
                }
                // Check the media type
                if (disk->flags & DISK_FLAG_REMOVABLE && curDisk >= 0x81)
                    disk->type = DISK_TYPE_CDROM;
                else if (curDisk >= 0x80)
                    disk->type = DISK_TYPE_HDD;
                else
                    disk->type = DISK_TYPE_FDD;
                NbLogMessageEarly (
                    "biosdisk: BIOS disk %#X found and working, size %llu, "
                    "type %d, flags %#X, sector size %u\r\n",
                    NEXBOOT_LOGLEVEL_DEBUG,
                    curDisk,
                    disk->size,
                    disk->type,
                    disk->flags,
                    disk->sectorSz);
            }
            NbLogMessageEarly ("biosdisk: Disk %#X found\r\n",
                               NEXBOOT_LOGLEVEL_INFO,
                               curDisk);
            ++curDisk;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &biosDiskSvcTab);
            break;
        }
    }
    return true;
}

static bool BiosDiskDumpData (void* objp, void* data)
{
    return true;
}

static bool BiosDiskNotify (void* objp, void* data)
{
    return true;
}

static bool BiosDiskReportError (void* objp, void* data)
{
    assert (objp);
    int error = (int) data;
    char buf[256];
    // Format message
    snprintf (buf, 256, "Disk error: %s", diskErrorStrs[error]);
    // Open log
    NbObject_t* log = NbObjFind ("/Interfaces/SysLog");
    assert (log);
    NbObjCallSvc (log, NB_LOG_WRITE, buf);
    return true;
}

static NbObjSvc biosDiskSvcs[] =
    {NULL, NULL, NULL, BiosDiskDumpData, BiosDiskNotify, BiosDiskReportError};
NbObjSvcTab_t biosDiskSvcTab = {ARRAY_SIZE (biosDiskSvcs), biosDiskSvcs};

NbDriver_t biosDiskDrv =
    {"BiosDisk", BiosDiskEntry, {0}, 0, false, sizeof (NbBiosDisk_t)};

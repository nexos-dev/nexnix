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
#include <nexboot/drivers/volume.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <stdio.h>
#include <string.h>

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

// CD-ROM spec packet
typedef struct _csp
{
    uint8_t sz;          // Size of packet
    uint8_t media;       // Boot media type
    uint8_t driveNum;    // Drive number
    uint8_t ctrl;        // Controller number
    uint32_t imgLba;     // LBA of emulated image
    uint8_t devSpec;
    uint16_t userBuf;
    uint16_t loadSeg;    // Load segment of image
    uint16_t imgSz;      // Size of boot image
    uint16_t cylSec;
    uint8_t headCount;
} __attribute__ ((packed)) NbCdromSpec_t;

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
static uint8_t curIter = 0;
static uint8_t bootDisk = 0;            // Boot disk. This is always drive 0
static bool bootDiskChecked = false;    // Wheter the boot disk has been checked
static NbBiosDisk_t* curDiskInfo = NULL;

// Converts LBA to CHS
static void lbaToChs (NbBiosDisk_t* disk, NbChsAddr_t* chsAddr, uint32_t lbaSect)
{
    uint32_t val = lbaSect / disk->spt;
    chsAddr->sector = (lbaSect % disk->spt) + 1;
    chsAddr->head = (val % disk->hpc);
    chsAddr->cylinder = (val / disk->hpc);
}

static void diskReset (uint8_t disk)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.dl = disk;
    in.ah = 0;
    NbBiosCall (0x13, &in, &out);
}

// Reads a sector without using LBA extensions
static uint8_t diskReadSector (uint8_t drive,
                               NbBiosDisk_t* disk,
                               void* buf,
                               uint32_t sector)
{
    NbBiosRegs_t in = {0}, out = {0};
    // Convert LBA to CHS
    NbChsAddr_t chsAddr;
    lbaToChs (disk, &chsAddr, sector);
    in.ah = 0x02;
    in.al = 1;
    in.ch = (uint8_t) chsAddr.cylinder;
    in.cl = chsAddr.sector | (chsAddr.cylinder & ~0x3F);
    in.dh = chsAddr.head;
    in.dl = disk->biosNum;
    in.es = NEXBOOT_BIOSBUF_BASE >> 4;
    in.bx = NEXBOOT_BIOSBUF_BASE & 0xF;
    // Try up to three times
    bool success = false;
    for (int i = 0; i < 3; ++i)
    {

        NbBiosCall (0x13, &in, &out);
        if (!(out.flags & NEXBOOT_CPU_CARRY_FLAG))
        {
            success = true;
            break;
        }
    }
    if (!success)
        return out.ah;
    // Copy buffer
    memcpy (buf, (void*) NEXBOOT_BIOSBUF_BASE, 512);
    return out.ah;
}

// Reads a sector using LBA extensions
static uint8_t diskReadSectorLba (uint8_t biosNum, void* buf, uint32_t sector)
{
    NbBiosRegs_t in = {0}, out = {0};
    NbBiosDap_t* dap = (NbBiosDap_t*) NEXBOOT_BIOSBUF2_BASE;
    memset (dap, 0, sizeof (NbBiosDap_t));
    dap->sz = 16;
    dap->bufOffset = NEXBOOT_BIOSBUF_BASE;
    dap->count = 1;
    dap->sector = sector;
    in.ah = 0x42;
    in.dl = biosNum;
    in.si = NEXBOOT_BIOSBUF2_BASE;
    NbBiosCall (0x13, &in, &out);
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
        return out.ah;
    // Copy buffer
    memcpy (buf, (void*) NEXBOOT_BIOSBUF_BASE, 512);
    return out.ah;
}

// Checks for LBA extensions
static bool diskCheckLba (NbBiosDisk_t* disk, uint8_t num)
{
    NbBiosRegs_t in = {0}, out = {0};
    // Check for LBA extensions on current device
    memset (&in, 0, sizeof (NbBiosRegs_t));
    memset (&out, 0, sizeof (NbBiosRegs_t));
    in.ah = BIOS_DISK_CHECK_LBA;
    in.bx = 0x55AA;
    in.dl = num;
    NbBiosCall (0x13, &in, &out);
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG || out.bx != 0xAA55)
        return false;
    disk->flags |= DISK_FLAG_LBA;
    // Check interfaces
    if (out.cx & BIOS_LBA_INTERFACE_EJECTING)
        disk->flags |= DISK_FLAG_EJECTABLE;
    if (out.cx & BIOS_LBA_INTERFACE_64BIT)
        disk->flags |= DISK_FLAG_64BIT;
    return true;
}

// Get disk type
static bool diskGetType (NbBiosDisk_t* disk, uint8_t num)
{
    NbBiosRegs_t in = {0}, out = {0};
    memset (&in, 0, sizeof (NbBiosRegs_t));
    memset (&out, 0, sizeof (NbBiosRegs_t));
    if (num >= 0x80)
    {
        in.ah = BIOS_DISK_GET_TYPE;
        in.al = 0xFF;
        in.dl = num;
        NbBiosCall (0x13, &in, &out);
        if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
            return false;
        if (out.ah == 0)
            disk->flags |= DISK_FLAG_REMOVABLE;    // Disk is removable
        disk->size = out.dx;
        disk->size |= (out.cx << 16);
        disk->type = DISK_TYPE_HDD;
    }
    else
    {
        disk->type = DISK_TYPE_FDD;
        disk->flags |= DISK_FLAG_REMOVABLE;
    }
    return true;
}

// Gets disk geometry
static bool diskGetGeometry (NbBiosDisk_t* disk, uint8_t num)
{
    NbBiosRegs_t in = {0}, out = {0};
    // Get geometry
    in.ah = BIOS_DISK_GET_PARAMS;
    in.dl = num;
    in.es = 0;
    in.di = 0;
    NbBiosCall (0x13, &in, &out);
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
        return false;
    disk->hpc = out.dh + 1;
    disk->spt = out.cl & 0x3F;
    uint32_t numCyls = ((out.cl >> 6) | out.ch) + 1;
    disk->size = (uint64_t) ((disk->spt * (disk->hpc - 1) * numCyls)) * 1024;
    return true;
}

// Gets DPT and sets disk information
static bool diskGetDptInfo (NbBiosDisk_t* disk, uint8_t num)
{
    NbBiosRegs_t in = {0}, out = {0};
    NbDriveParamTab_t* dpt = (NbDriveParamTab_t*) NEXBOOT_BIOSBUF_BASE;
    dpt->sz = sizeof (NbDriveParamTab_t);
    in.ah = BIOS_DISK_GET_DPT;
    in.dl = num;
    in.si = ((uint32_t) dpt & 0x0F);
    in.ds = ((uint32_t) dpt >> 4);
    NbBiosCall (0x13, &in, &out);
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
        return false;
    assert (dpt->sz == sizeof (NbDriveParamTab_t));
    // Set fields
    disk->sectorSz = dpt->bytesPerSector;
    disk->size = dpt->diskSz * disk->sectorSz;
    if (dpt->flags & DPT_FLAG_MEDIA_REMOVABLE)
        disk->flags |= DISK_FLAG_REMOVABLE;
    // Check the media type
    if (disk->flags & DISK_FLAG_REMOVABLE && curDisk >= 0x81)
        disk->type = DISK_TYPE_CDROM;
    else if (curDisk >= 0x80)
        disk->type = DISK_TYPE_HDD;
    else
        disk->type = DISK_TYPE_FDD;
    return true;
}

static bool BiosDiskEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {    // Set bootDisk
            if (!bootDisk)
            {
                NbObject_t* sysInfo = NbObjFind ("/Devices/Sysinfo");
                assert (sysInfo);
                bootDisk = ((NbSysInfo_t*) (NbObjGetData (sysInfo)))->bootDrive;
            }
            NbBiosDisk_t* disk = params;
            memset (disk, 0, sizeof (NbBiosDisk_t));
            disk->hdr.devId = curDisk;
            disk->hdr.devSubType = 0;
            NbBiosRegs_t in = {0}, out = {0};
        checkDisk : {
            // If boot disk hasn't been checked, check it
            if (!bootDiskChecked)
                curDisk = bootDisk;
            else
            {
                // If this disk equal boot disk, move to next disk
                if (curDisk == bootDisk)
                    ++curDisk;
            }
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
                    memset (&out, 0, sizeof (NbBiosRegs_t));
                    in.ah = 0x02;
                    in.al = 1;
                    in.cl = 1;
                    in.dl = curDisk;
                    in.es = NEXBOOT_BIOSBUF_BASE >> 4;
                    in.bx = NEXBOOT_BIOSBUF_BASE & 0xF;
                    NbBiosCall (0x13, &in, &out);
                    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                    {
                        assert (bootDiskChecked);
                        NbLogMessageEarly (
                            "biosdisk: BIOS disk %#X doesn't exist\r\n",
                            NEXBOOT_LOGLEVEL_DEBUG,
                            curDisk);
                        // No disks left to check, move to hard disks
                        curDisk = 0x80;
                        // If this disk equal boot disk, move to next disk
                        if (curDisk == bootDisk)
                            ++curDisk;
                    }
                    else
                        diskSuccess = true;    // We found a disk
                }
                else
                {
                    // Read using LBA
                    if (diskReadSectorLba (curDisk, (void*) NEXBOOT_BIOSBUF_BASE, 0))
                    {
                        // Try without LBA
                        memset (&in, 0, sizeof (NbBiosRegs_t));
                        memset (&out, 0, sizeof (NbBiosRegs_t));
                        in.ah = 0x02;
                        in.al = 1;
                        in.cl = 1;
                        in.dl = curDisk;
                        in.es = NEXBOOT_BIOSBUF_BASE >> 4;
                        in.bx = NEXBOOT_BIOSBUF_BASE & 0xF;
                        NbBiosCall (0x13, &in, &out);
                        if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
                        {
                            assert (bootDiskChecked);
                            NbLogMessageEarly (
                                "biosdisk: BIOS disk %#X doesn't exist\r\n",
                                NEXBOOT_LOGLEVEL_DEBUG,
                                curDisk);
                            return false;
                        }
                        else
                            diskSuccess = true;
                    }
                    else
                        diskSuccess = true;    // We found a disk
                }
            } while (!diskSuccess);
            disk->biosNum = curDisk;
        }
            // Check for LBA extensions on current device
            if (!diskCheckLba (disk, curDisk))
            {
                assert (curDisk <= 0x8A);
                // Check using get disk type function
                if (!diskGetType (disk, curDisk))
                {
                    ++curDisk;
                    goto checkDisk;
                }
                if (!diskGetGeometry (disk, curDisk))
                {
                    ++curDisk;
                    goto checkDisk;
                }
                disk->sectorSz = 512;
            }
            else
            {
                NbLogMessageEarly ("biosdisk: Disk supports LBA extensions\r\n",
                                   NEXBOOT_LOGLEVEL_DEBUG);
                if (!diskGetDptInfo (disk, curDisk))
                {
                    NbLogMessageEarly ("biosdisk: Disk %#X not working\r\n",
                                       NEXBOOT_LOGLEVEL_DEBUG,
                                       curDisk);
                    ++curDisk;
                    goto checkDisk;    // Retry
                }
            }
            NbLogMessageEarly (
                "biosdisk: BIOS disk %#X found and working, size %llu, "
                "type %d, flags %#X, sector size %u\r\n",
                NEXBOOT_LOGLEVEL_DEBUG,
                curDisk,
                disk->size,
                disk->type,
                disk->flags,
                disk->sectorSz);
            NbLogMessageEarly ("biosdisk: Disk %#X found\r\n",
                               NEXBOOT_LOGLEVEL_INFO,
                               curDisk);
            // If this is boot disk, do normal disk check now
            disk->hdr.devId = curIter;
            disk->hdr.sz = sizeof (NbBiosDisk_t);
            if (!bootDiskChecked)
            {
                bootDiskChecked = true;
                curDisk = 0;
            }
            else
                ++curDisk;
            ++curIter;
            curDiskInfo = disk;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &biosDiskSvcTab);
            NbDiskInfo_t* diskInf = (NbDiskInfo_t*) malloc (sizeof (NbDiskInfo_t));
            assert (diskInf);
            diskInf->flags = curDiskInfo->flags;
            diskInf->sectorSz = curDiskInfo->sectorSz;
            diskInf->size = curDiskInfo->size;
            diskInf->type = curDiskInfo->type;
            diskInf->internal = curDiskInfo;
            NbObjSetData (obj, diskInf);
            // Create the volumes
            NbDriver_t* volMgr = NbFindDriver ("VolManager");
            assert (volMgr);
            NbSendDriverCode (volMgr, VOLUME_ADD_DISK, obj);
            break;
        }
    }
    return true;
}

static bool BiosDiskReadSectors (void* obj, void* data)
{
    NbDiskInfo_t* disk = NbObjGetData ((NbObject_t*) obj);
    assert (disk);
    NbBiosDisk_t* biosDisk = disk->internal;
    NbReadSector_t* readInf = data;
    void* buf = readInf->buf;
    for (int i = 0; i < readInf->count; ++i)
    {
        // Determine if we can use LBA
        if (biosDisk->flags & DISK_FLAG_LBA)
        {
            int res = 0;
            if ((res = diskReadSectorLba (biosDisk->biosNum, buf, readInf->sector)))
            {
                readInf->error = res;
                return false;
            }
        }
        else
        {
            // Use non-LBA function
            int res = 0;
            if ((res = diskReadSector (biosDisk->biosNum,
                                       biosDisk,
                                       buf,
                                       readInf->sector)))
            {
                readInf->error = res;
                return false;
            }
        }
        buf += disk->sectorSz;
    }
    readInf->error = DISK_ERROR_NOERROR;
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

static NbObjSvc biosDiskSvcs[] = {NULL,
                                  NULL,
                                  NULL,
                                  BiosDiskDumpData,
                                  BiosDiskNotify,
                                  BiosDiskReportError,
                                  BiosDiskReadSectors};
NbObjSvcTab_t biosDiskSvcTab = {ARRAY_SIZE (biosDiskSvcs), biosDiskSvcs};

NbDriver_t biosDiskDrv =
    {"BiosDisk", BiosDiskEntry, {0}, 0, false, sizeof (NbBiosDisk_t)};

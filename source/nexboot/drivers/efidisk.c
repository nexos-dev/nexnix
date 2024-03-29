/*
    efidisk.c - contains EFI disk driver
    Copyright 2023 - 2024 The NexNix Project

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
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <stdio.h>
#include <string.h>

extern NbObjSvcTab_t efiDiskSvcTab;
extern NbDriver_t efiDiskDrv;

// Disk structure
typedef struct _efidisk
{
    NbDiskInfo_t disk;              // General info about disk
    EFI_HANDLE diskHandle;          // Disk handle
    EFI_BLOCK_IO_PROTOCOL* prot;    // Disk protocol
    EFI_DEVICE_PATH* device;        // Device path of disk device
    uint32_t mediaId;               // Media ID at detection time
} NbEfiDisk_t;

// List of disk handles
static EFI_HANDLE* diskHandles = NULL;
// Number of handles
static size_t numHandles = 0;
// Current handle we are working on
static int curHandle = 0;
// Current disk number
static int diskNum = 0;
// Disk protocol GUID
static EFI_GUID blockIoGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
// Temporary aligned buffer
static void* tempBuf = NULL;

// Driver entry
static bool EfiDiskEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_START:
            // Locate handles
            diskHandles = NbEfiLocateHandle (&blockIoGuid, &numHandles);
            if (!diskHandles)
            {
                NbLogMessage ("nbefidisk: No disks found\r\n", NEXBOOT_LOGLEVEL_WARNING);
                numHandles = 0;    // Maybe slightly pedantic
            }
            else
            {
                tempBuf = (void*) NbFwAllocPages ((64 * 1024) / NEXBOOT_CPU_PAGE_SIZE);
                assert (tempBuf);
            }
            break;
        case NB_DRIVER_ENTRY_DETECTHW: {
            // Get to an actual disk, not partition
            bool foundDisk = false;
            while (!foundDisk)
            {
                // Check if we've alread exahusted all handles
                if (curHandle == numHandles)
                {
                    NbEfiFreePool (diskHandles);
                    return false;
                }
                EFI_BLOCK_IO_PROTOCOL* prot =
                    NbEfiOpenProtocol (diskHandles[curHandle], &blockIoGuid);
                if (!prot)
                {
                    NbLogMessage ("nbefidisk: Unable to open block I/O protocol on handle\r\n",
                                  NEXBOOT_LOGLEVEL_ERROR);
                    ++curHandle;
                    continue;
                }
                if (prot->Media->LogicalPartition)
                {
                    // Go to next
                    ++curHandle;
                }
                // Check if sector size is supported
                else if (prot->Media->BlockSize > 4096)
                {
                    ++curHandle;
                }
                else
                {
                    // Check if media is present
                    if (!prot->Media->MediaPresent)
                    {
                        // Go to next
                        ++curHandle;
                    }
                    else
                        foundDisk = true;
                }
            }
            // Prepare structure
            NbEfiDisk_t* disk = params;
            disk->diskHandle = diskHandles[curHandle];
            disk->prot = NbEfiOpenProtocol (diskHandles[curHandle], &blockIoGuid);
            if (!disk->prot)
            {
                NbLogMessage ("nbefidisk: Unable to open block I/O protocol on disk %d\r\n",
                              NEXBOOT_LOGLEVEL_ERROR,
                              diskNum);
                return false;
            }
            disk->mediaId = disk->prot->Media->MediaId;
            disk->disk.sectorSz = disk->prot->Media->BlockSize;
            disk->disk.size = (disk->prot->Media->LastBlock + 1) * disk->disk.sectorSz;
            // Figure out type
            EFI_DEVICE_PATH* dev = NbEfiGetDevicePath (disk->diskHandle);
            disk->device = dev;
            EFI_DEVICE_PATH* lastDev = NbEfiGetLastDev (dev);
            // If this is a messaging type, than it's either a CD-ROM or HDD of flash drive
            if (lastDev->Type == 3)
            {
                // Check subtype
                if (lastDev->SubType == 5 || lastDev->SubType == 16)
                    disk->disk.type = DISK_TYPE_HDD;    // We treat flash drives as HDDs
                else if (lastDev->SubType == 1)
                    disk->disk.type = DISK_TYPE_CDROM;    // ATAPI is CD-ROM
                else if (lastDev->SubType == 18)
                {
                    // This could be an HDD or CD-ROM. Check write protection to determine
                    if (disk->prot->Media->ReadOnly)
                        disk->disk.type = DISK_TYPE_CDROM;
                    else
                        disk->disk.type = DISK_TYPE_HDD;
                }
            }
            // Check for hardware types
            if (lastDev->Type == 1)
            {
                // One of my copmuters has a flash drive with a vendor-defined device path
                if (lastDev->SubType == 4 || lastDev->SubType == 1)
                    disk->disk.type = DISK_TYPE_HDD;
            }
            // Check based on ACPI types
            if (lastDev->Type == 2)
            {
                if (lastDev->SubType == 1)
                    disk->disk.type = DISK_TYPE_FDD;    // Probably an FDD
            }
            // Determine flags
            disk->disk.flags = 0;
            if (disk->prot->Media->RemovableMedia)
                disk->disk.flags |= DISK_FLAG_REMOVABLE;
            disk->disk.flags |= DISK_FLAG_LBA;
            disk->disk.flags |= DISK_FLAG_64BIT;
            // Print results
            NbLogMessage ("nbefidisk: Found disk %d with size %lluMiB, type %d, flags %#02X, "
                          "sector size %u\r\n",
                          NEXBOOT_LOGLEVEL_INFO,
                          diskNum,
                          disk->disk.size / 1024 / 1024,
                          disk->disk.type,
                          disk->disk.flags,
                          disk->disk.sectorSz);
            disk->disk.dev.devId = diskNum;
            disk->disk.dev.sz = sizeof (NbEfiDisk_t);
            ++curHandle;
            ++diskNum;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &efiDiskSvcTab);
            NbObjSetManager (obj, &efiDiskDrv);
            // Create the volumes
            NbDriver_t* volMgr = NbFindDriver ("VolManager");
            assert (volMgr);
            NbSendDriverCode (volMgr, VOLUME_ADD_DISK, obj);
            break;
        }
    }
    return true;
}

static bool EfiDiskNotify (void* objp, void* params)
{
    return true;
}

static bool EfiDiskDumpData (void* objp, void* data)
{
    NbObject_t* obj = objp;
    NbEfiDisk_t* disk = NbObjGetData (obj);
    void (*writeData) (const char* fmt, ...) = data;
    writeData ("Disk type: ");
    if (disk->disk.type == DISK_TYPE_HDD)
        writeData ("hard disk\n");
    else if (disk->disk.type == DISK_TYPE_FDD)
        writeData ("floppy disk\n");
    else if (disk->disk.type == DISK_TYPE_CDROM)
        writeData ("CD-ROM\n");
    writeData ("Disk size (MiB): %u\n", (disk->disk.size / 1024) / 1024);
    writeData ("Sector size: %d\n", disk->disk.sectorSz);
    writeData ("Flags: ");
    if (disk->disk.flags & DISK_FLAG_64BIT)
        writeData ("64-bit ");
    if (disk->disk.flags & DISK_FLAG_LBA)
        writeData ("LBA ");
    if (disk->disk.flags & DISK_FLAG_EJECTABLE)
        writeData ("ejectable ");
    if (disk->disk.flags & DISK_FLAG_REMOVABLE)
        writeData ("removable ");
    writeData ("\n");
    return true;
}

static bool EfiDiskReportError (void* objp, void* params)
{
    assert (objp);
    NbLogMessage ("nbefidisk: Disk error\r\n", NEXBOOT_LOGLEVEL_CRITICAL);
    return true;
}

static bool EfiDiskReadSectors (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbEfiDisk_t* disk = NbObjGetData (obj);
    NbReadSector_t* sect = params;
    EFI_STATUS status;
    if ((status = disk->prot->ReadBlocks (disk->prot,
                                          disk->mediaId,
                                          sect->sector,
                                          sect->count * disk->disk.sectorSz,
                                          tempBuf)) != EFI_SUCCESS)
    {
        return false;
    }
    // Copy data from aligned buffer to dest buffer
    memcpy (sect->buf, tempBuf, sect->count * disk->disk.sectorSz);
    return true;
}

static NbObjSvc efiDiskSvcs[] =
    {NULL, NULL, NULL, EfiDiskDumpData, EfiDiskNotify, EfiDiskReportError, EfiDiskReadSectors};
NbObjSvcTab_t efiDiskSvcTab = {ARRAY_SIZE (efiDiskSvcs), efiDiskSvcs};

NbDriver_t efiDiskDrv = {"EfiDisk", EfiDiskEntry, {0}, 0, false, sizeof (NbEfiDisk_t)};

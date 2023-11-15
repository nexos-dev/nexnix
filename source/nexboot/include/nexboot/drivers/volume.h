/*
    volume.h - contains volume header
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

#ifndef _VOLUME_H
#define _VOLUME_H

#include <nexboot/drivers/disk.h>
#include <nexboot/object.h>
#include <stdbool.h>
#include <stdint.h>

typedef NbReadSector_t NbReadBlock_t;

// Volume structure
typedef struct _vol
{
    int number;          // Volume number
    NbObject_t* disk;    // Disk this volume is attached to
    bool isPartition;    // Wheter this volume represents a partition or a whole disk
    bool isActive;       // If this volume is active
    uint32_t volStart;      // Start sector of volume
    uint64_t volSize;       // Size of volume
    uint32_t volFileSys;    // Filesystem of volume
} NbVolume_t;

// Volume drivers codes
#define VOLUME_ADD_DISK (NB_DRIVER_USER)

// Volume filesystems
#define VOLUME_FS_FAT12   1
#define VOLUME_FS_FAT16   2
#define VOLUME_FS_FAT32   3
#define VOLUME_FS_EXT2    4
#define VOLUME_FS_FAT     5
#define VOLUME_FS_ISO9660 6

// Volume object services
#define NB_VOLUME_READ_SECTORS 5

// Helper routine to get boot volume from disk object
NbObject_t* NbGetBootVolume (NbObject_t* disk);

#endif

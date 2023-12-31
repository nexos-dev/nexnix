/*
    disk.h - contains BIOS disk driver header
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

#ifndef _DISK_H
#define _DISK_H

#include <nexboot/fw.h>
#include <stdint.h>

// Geometry structure
typedef struct _diskaddr
{
    uint8_t sector;
    uint8_t head;
    uint16_t cylinder;
} NbChsAddr_t;

typedef struct _diskinf
{
    NbHwDevice_t dev;
    uint8_t flags;        // Disk flags
    uint64_t size;        // Size of disk in sectors
    uint32_t sectorSz;    // Size of a sector
    uint16_t type;        // Type of disk
    void* internal;       // Internal disk info
} NbDiskInfo_t;

#define DISK_FLAG_LBA       (1 << 0)
#define DISK_FLAG_REMOVABLE (1 << 1)
#define DISK_FLAG_EJECTABLE (1 << 2)
#define DISK_FLAG_64BIT     (1 << 3)

#define DISK_TYPE_HDD   1
#define DISK_TYPE_FDD   2
#define DISK_TYPE_CDROM 3

// Disk functions
#define NB_DISK_REPORT_ERROR 5
#define NB_DISK_READ_SECTORS 6

// Read sector packet
typedef struct _readsect
{
    uint64_t sector;    // Sector to read
    int count;          // Number to read
    void* buf;          // Buffer to read into
    int error;          // Error code result
} NbReadSector_t;

#endif

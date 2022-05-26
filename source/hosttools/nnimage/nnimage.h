/*
    nnimage.h - contains private functions for nnimage
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

/// @file nnimage.h

#ifndef _NNIMAGE_H
#define _NNIMAGE_H

#include <libnex.h>

/// Valid image formats
#define IMG_FORMAT_MBR     1
#define IMG_FORMAT_GPT     2
#define IMG_FORMAT_ISO9660 3
#define IMG_FORMAT_FLOPPY  4

// Name table
static const char* partTypeNames[] = {"", "MBR", "GPT", "ISO9660", "floppy"};

/// An image in the list
typedef struct _image
{
    const char* name;         ///< The name of this image
    uint32_t sz;              ///< Size of the image
    uint32_t mul;             ///< Multiplier of size
    uint32_t sectSz;          ///< Size of a sector
    char* file;               ///< File to read from
    short format;             ///< Format of image
    int fileNo;               ///< Underlying file descriptor
    int partCount;            ///< Number of partitions on this image
    short bootMode;           ///< Boot mode of this image
    ListHead_t* partsList;    ///< List of partitions
} Image_t;

/// Valid filesystems
#define IMG_FILESYS_FAT32   1
#define IMG_FILESYS_FAT16   2
#define IMG_FILESYS_FAT12   3
#define IMG_FILESYS_EXT2    4
#define IMG_FILESYS_ISO9660 5

// Name table
static const char* fsTypeNames[] = {"", "FAT32", "FAT16", "FAT12", "ext2", "ISO9660"};

// Boot modes
#define IMG_BOOTMODE_BIOS   1
#define IMG_BOOTMODE_EFI    2
#define IMG_BOOTMODE_HYBRID 4

/// Internal partition. This is used to give lower level info down to the file system drivers
typedef struct _partint
{
    uint64_t lbaStart;    ///< LBA value of partition start
    uint64_t lbaSz;       ///< Size of partition in LBA
} partInternal_t;

/// A partition
typedef struct _part
{
    const char* name;           ///< Name of partition
    uint32_t sz;                ///< Size of partition
    uint32_t start;             ///< Start location of partition
    short filesys;              ///< Filesystem of partition
    char* prefix;               ///< Prefix directory of partition
    bool isBootPart;            ///< If this is the boot partition
    partInternal_t internal;    ///< Interal partition data
} Partition_t;

/// Creates list if images and their respective partitions
ListHead_t* createImageList (ListHead_t* confBlocks);

/// Returns image list
ListHead_t* getImages();

/// Creates images, partitions, and filesystems, and copies files
bool createImages (ListHead_t* images, const char* action, bool overwrite, const char* file);

/// Creates an MBR partition table in img
bool createMbr (Image_t* img);

/// Writes a sector to disk image
bool writeSector (Image_t* img, void* data, uint32_t sector);

/// Reads in a sector from the disk image
bool readSector (Image_t* img, void* buf, uint32_t sector);

/// Adds partition to MBR
bool addMbrPartition (Image_t* img, Partition_t* part);

/// Mounts an MBR partiiton
void mountMbrPartition (Image_t* img, Partition_t* part);

/// Adds partition to MBR at specified location
bool addMbrPartitionAt (Image_t* img, Partition_t* part, uint32_t* nextSector);

/// Cleans up MBR data
bool cleanupMbr (Image_t* img);

/// Creates the GPT
bool createGpt (Image_t* img);

/// Cleanups GPT data
bool cleanupGpt (Image_t* img);

/// Adds a GPT protection partition to MBR
bool addMbrProtectivePartition (Image_t* img);

/// Adds a GPT partitions
bool addGptPartition (Image_t* img, Partition_t* part);

/// Mounts a GPT partiiton
void mountGptPartition (Image_t* img, Partition_t* part);

/// Formats a floppy disk in FAT12
bool formatFatFloppy (Image_t* img, Partition_t* part);

/// Cleans up the state of FAT file system driver
bool cleanupFat (Image_t* img, Partition_t* part);

/// Formats a hard disk partition in FAT16
bool formatFat16 (Image_t* img, Partition_t* part);

/// Mounts a FAT 12 or 16 file system
bool mountFat (Image_t* img, Partition_t* part);

/// Copies a file to a FAT12 file system
bool copyFileFatFloppy (Image_t* img, const char* src, const char* dest);

#endif

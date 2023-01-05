/*
    nnimage.h - contains private functions for nnimage
    Copyright 2022, 2023 The NexNix Project

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

#include <guestfs.h>
#include <libnex.h>

/// Valid image formats
#define IMG_FORMAT_MBR     1
#define IMG_FORMAT_GPT     2
#define IMG_FORMAT_ISO9660 3
#define IMG_FORMAT_FLOPPY  4

// Name table
static const char* partTypeNames[] = {"", "mbr", "gpt", "ISO9660", "floppy"};

/// An image in the list
typedef struct _image
{
    const char* name;             ///< The name of this image
    uint32_t sz;                  ///< Size of the image
    uint32_t mul;                 ///< Multiplier of size
    char* file;                   ///< File to read from
    short format;                 ///< Format of image
    int partCount;                ///< Number of partitions on this image
    short bootMode;               ///< Boot mode of this image
    short bootEmu;                ///< Emulation mode of image (on ISO images)
    bool isUniversal;             ///< Is it a universal disk image?
    char* mbrFile;                /// Path to file used for MBR
    guestfs_h* guestFs;           ///< Handle to libguestfs instance
    ListHead_t* partsList;        ///< List of partitions
    struct _part* bootPart;       ///< Boot partition
    struct _part* altBootPart;    ///< Alternate boot partition
} Image_t;

/// Valid filesystems
#define IMG_FILESYS_FAT32   1
#define IMG_FILESYS_FAT16   2
#define IMG_FILESYS_FAT12   3
#define IMG_FILESYS_EXT2    4
#define IMG_FILESYS_ISO9660 5

// Name table for file systems
static const char* fsTypeNames[] =
    {"", "FAT32", "FAT16", "FAT12", "ext2", "ISO9660"};

// MBR partition byte IDs
static uint8_t mbrByteIds[] = {0, 0x0C, 0x0E, 0x01, 0x83, 0};

// GPT partition GUIDs
static const char* gptGuids[] = {"",
                                 "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7",
                                 "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7",
                                 "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7",
                                 "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7",
                                 ""};

// Valid multipliers
#define IMG_MUL_KIB 1
#define IMG_MUL_MIB 2
#define IMG_MUL_GIB 3

// Multiplier sizes
static uint32_t muls[] = {0, 1024, 1024 * 1024, 1024 * 1024 * 1024};

// Multiplier names
static const char* mulNames[] = {"", "KiB", "MiB", "GiB"};

// Size of a sector
#define IMG_SECT_SZ 512

// Boot modes
#define IMG_BOOTMODE_BIOS   1
#define IMG_BOOTMODE_EFI    2
#define IMG_BOOTMODE_HYBRID 3
#define IMG_BOOTMODE_NOBOOT 4

// Boot mode names
static const char* bootModeNames[] = {"", "bios", "efi", "hybrid", "noboot"};

// Boot emulations
#define IMG_BOOTEMU_FDD  1
#define IMG_BOOTEMU_HDD  2
#define IMG_BOOTEMU_NONE 3

// Boot emulation names
static const char* bootEmuNames[] = {"", "fdd", "hdd", "noemu"};

/// A partition
typedef struct _part
{
    const char* name;      ///< Name of partition
    uint32_t sz;           ///< Size of partition
    uint32_t start;        ///< Start location of partition
    short filesys;         ///< Filesystem of partition
    char* prefix;          ///< Prefix directory of partition
    char* vbrFile;         ///< Path to file used for VBR
    bool isBootPart;       ///< If this is the boot partition
    bool isAltBootPart;    ///< If this is the alternate boot partition
} Partition_t;

/// Creates list if images and their respective partitions
ListHead_t* createImageList (ListHead_t* confBlocks);

/// Returns image list
ListHead_t* getImages();

/// Gets boot partition pointer
Partition_t* getBootPart (Image_t* img);

/// Gets alternate boot partition pointer
Partition_t* getAltBootPart (Image_t* img);

/// Creates images, partitions, and filesystems, and copies files
bool createImages (ListHead_t* images,
                   const char* action,
                   bool overwrite,
                   const char* file,
                   const char* listFile);

/// Update a partition's files
bool updatePartition (Image_t* img,
                      Partition_t* part,
                      const char* listFile,
                      const char* mount,
                      const char* host);

/// Updates the VBR of a partition
bool updateVbr (Image_t* img, Partition_t* part);

/// Updates the MBR of a partition
bool updateMbr (Image_t* img);

#endif

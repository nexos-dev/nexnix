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
#define IMG_FORMAT_MBR     0
#define IMG_FORMAT_GPT     1
#define IMG_FORMAT_ISO9660 2
#define IMG_FORMAT_FLOPPY  3

/// An image in the list
typedef struct _image
{
    const char* name;         ///< The name of this image
    uint32_t sz;              ///< Size of the image
    uint16_t mul;             ///< Multiplier of size
    const char* file;         ///< File to read from
    short format;             ///< Format of image
    ListHead_t* partsList;    ///< List of partitions
} Image_t;

/// Valid partition formats
#define IMG_FORMAT_FAT32 0
#define IMG_FORMAT_FAT12 1
#define IMG_FORMAT_EXT2  2
#define IMG_FORMAT_UDF   3

/// A partition
typedef struct _part
{
    const char* id;      ///< Identifier of image
    const char* name;    ///< Name of partition
    uint32_t sz;         ///< Size of partition
    uint32_t start;      ///< Start location of partition
    short format;        ///< Filesystem of partition
    char* prefix;        ///< Prefix directory on host of partition
} Partition_t;

/// Creates list if images and their respective partitions
ListHead_t* createImageList (ListHead_t* confBlocks);

#endif

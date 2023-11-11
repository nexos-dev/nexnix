/*
    fstable.h - contains function tables for each driver
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

#ifndef _FSTABLE_H
#define _FSTABLE_H

#include "include/fat.h"
#include <nexboot/vfs.h>
#include <stdint.h>

// FS drivers
#define FS_DRIVER_FAT     0
#define FS_DRIVER_EXT2    1
#define FS_DRIVER_ISO9660 2

// Filesystem function types
typedef bool (*FsMountT) (NbObject_t*);
typedef bool (*FsOpenFileT) (NbObject_t*, NbFile_t*);
typedef bool (*FsCloseFileT) (NbObject_t*, NbFile_t*);
typedef bool (*FsReadBlockT) (NbObject_t*, NbFile_t*, uint32_t);

// Function tables
FsMountT mountTable[] = {FatMountFs};
FsOpenFileT openFileTable[] = {FatOpenFile};
FsCloseFileT closeFileTable[] = {FatCloseFile};
FsReadBlockT readBlockTable[] = {FatReadFileBlock};

#define FsMount(type, fs)                (mountTable[(type)](fs))
#define FsOpenFile(type, fs, file)       (openFileTable[(type)](fs, file))
#define FsCloseFile(type, fs, file)      (closeFileTable[(type)](fs, file))
#define FsReadBlock(type, fs, file, pos) (readBlockTable[(type)](fs, file, pos))

#endif

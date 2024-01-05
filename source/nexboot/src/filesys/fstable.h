/*
    fstable.h - contains function tables for each driver
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

#ifndef _FSTABLE_H
#define _FSTABLE_H

#include "include/fat.h"
#include "include/iso9660.h"
#include <nexboot/vfs.h>
#include <stdint.h>

// FS drivers
#define FS_DRIVER_FAT     0
#define FS_DRIVER_ISO9660 1

// Filesystem function types
typedef bool (*FsMountT) (NbObject_t*);
typedef bool (*FsUnmountT) (NbObject_t*);
typedef bool (*FsOpenFileT) (NbObject_t*, NbFile_t*);
typedef bool (*FsCloseFileT) (NbObject_t*, NbFile_t*);
typedef bool (*FsGetFileInfoT) (NbObject_t*, NbFileInfo_t*);
typedef bool (*FsReadBlockT) (NbObject_t*, NbFile_t*, uint32_t);
typedef bool (*FsGetDirT) (NbObject_t*, const char*, NbDirIter_t*);
typedef bool (*FsReadDirT) (NbObject_t*, NbDirIter_t*);

// Function tables
FsMountT mountTable[] = {FatMountFs, IsoMountFs};
FsUnmountT unmountTable[] = {FatUnmountFs, IsoUnmountFs};
FsOpenFileT openFileTable[] = {FatOpenFile, IsoOpenFile};
FsCloseFileT closeFileTable[] = {FatCloseFile, IsoCloseFile};
FsGetFileInfoT getInfoTable[] = {FatGetFileInfo, IsoGetFileInfo};
FsReadBlockT readBlockTable[] = {FatReadFileBlock, IsoReadFileBlock};
FsGetDirT getDirTable[] = {FatGetDir, IsoGetDir};
FsReadDirT readDirTable[] = {FatReadDir, IsoReadDir};

#define FsMount(type, fs)                (mountTable[(type)](fs))
#define FsUnmount(type, fs)              (unmountTable[(type)](fs))
#define FsOpenFile(type, fs, file)       (openFileTable[(type)](fs, file))
#define FsCloseFile(type, fs, file)      (closeFileTable[(type)](fs, file))
#define FsGetFileInfo(type, fs, info)    (getInfoTable[(type)](fs, info));
#define FsReadBlock(type, fs, file, pos) (readBlockTable[(type)](fs, file, pos))
#define FsGetDir(type, fs, name, iter)   (getDirTable[(type)](fs, name, iter))
#define FsReadDir(type, fs, iter)        (readDirTable[(type)](fs, iter))

#endif

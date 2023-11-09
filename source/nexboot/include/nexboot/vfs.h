/*
    vfs.h - contains virtual filesystem for nexboot
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

#ifndef _VFS_H
#define _VFS_H

#include <libnex/list.h>
#include <nexboot/drivers/volume.h>
#include <nexboot/object.h>

#define VFS_NAME_MAX 256

typedef struct _file
{
    Object_t obj;               // Libnex object
    NbObject_t* fileSys;        // Filesystem object
    char name[VFS_NAME_MAX];    // Name of file
    uint32_t pos;               // Position pointer
    void* internal;             // Internal data
} NbFile_t;

// Filesystem structure
typedef struct _fs
{
    NbObject_t* volume;    // Volume FS is on
    ListHead_t* files;     // List of open files
    int type;              // File system type
    int driver;            // FS driver ID of FS
    uint16_t blockSz;      // Block size in file system
    void* internal;        // Internal FS data
} NbFileSys_t;

// Mounts filesystem on volume
NbObject_t* NbVfsMountFs (NbObject_t* volume, const char* name);

// Unmounts filesystem
bool NbVfsUnMountFs (NbObject_t* fs);

// Object opertaions
#define NB_VFS_OPEN_FILE 5

typedef struct _openfile
{
    NbFile_t* file;
    const char* name;
} NbOpenFileOp_t;

#endif

/*
    vfs.h - contains virtual filesystem for nexboot
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

#ifndef _VFS_H
#define _VFS_H

#include <libnex/list.h>
#include <nexboot/drivers/volume.h>
#include <nexboot/object.h>

#define VFS_NAME_MAX 256

typedef struct _file
{
    Object_t obj;               // Libnex object
    int fileId;                 // ID of file
    NbObject_t* fileSys;        // Filesystem object
    char name[VFS_NAME_MAX];    // Name of file
    uint32_t pos;               // Position pointer
    uint32_t size;              // Size of file
    void* internal;             // Internal data
    void* blockBuf;             // Buffer for one read block
} NbFile_t;

// File into
typedef struct _fileInfo
{
    NbObject_t* fileSys;        // Filesystem of file
    char name[VFS_NAME_MAX];    // Name of file
    uint32_t size;              // Size of file
    int type;                   // File type
} NbFileInfo_t;

#define NB_FILE_FILE 0
#define NB_FILE_DIR  1

// Directory iterator
typedef struct _diriter
{
    char name[VFS_NAME_MAX];    // Name of entry
    int type;                   // Type of entry
    uint8_t internal[16];       // Internal info
} NbDirIter_t;

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
bool NbVfsUnmount (NbObject_t* fs);

// Object opertaions
#define NB_VFS_OPEN_FILE     5
#define NB_VFS_CLOSE_FILE    6
#define NB_VFS_READ_FILE     7
#define NB_VFS_SEEK_FILE     8
#define NB_VFS_GET_FILE_INFO 9
#define NB_VFS_GET_DIR       10
#define NB_VFS_READ_DIR      11

typedef struct _openfile
{
    NbFile_t* file;
    const char* name;
} NbOpenFileOp_t;

typedef struct _getdir
{
    const char* path;     // Path of directory
    NbDirIter_t* iter;    // Iterator to file out
} NbGetDirOp_t;

typedef struct _seekfile
{
    NbFile_t* file;
    uint32_t pos;
    bool relative;
} NbSeekOp_t;

typedef struct _readfile
{
    NbFile_t* file;
    uint32_t count;
    void* buf;
    size_t bytesRead;
} NbReadOp_t;

#endif

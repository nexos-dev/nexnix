/*
    vfs.c - contains virtual filesystem for nexboot
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

#include "filesys/fstable.h"
#include <assert.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdio.h>
#include <string.h>

extern NbObjSvcTab_t fsSvcTab;

// Converts file system type to driver
static int fsTypeToDriver (int type)
{
    switch (type)
    {
        case VOLUME_FS_FAT:
        case VOLUME_FS_FAT12:
        case VOLUME_FS_FAT16:
        case VOLUME_FS_FAT32:
            return FS_DRIVER_FAT;
        case VOLUME_FS_EXT2:
            return FS_DRIVER_EXT2;
        case VOLUME_FS_ISO9660:
            return FS_DRIVER_ISO9660;
        default:
            assert (0);
    }
}

// Mounts a filesystem. Takes a volume object as argument, and returns a filesystem
// object
NbObject_t* NbVfsMountFs (NbObject_t* volObj, const char* name)
{
    NbVolume_t* vol = NbObjGetData (volObj);
    // Create file system object
    if (!NbObjFind ("/Interfaces/FileSys"))
        assert (NbObjCreate ("/Interfaces/FileSys", OBJ_TYPE_DIR, 0));
    char buf[64];
    snprintf (buf, 64, "/Interfaces/FileSys/%s", name);
    NbObject_t* fsObj = NbObjCreate (buf, OBJ_TYPE_FS, 0);
    if (!fsObj)
        return NULL;
    // Initialize data structure
    NbFileSys_t* fs = (NbFileSys_t*) malloc (sizeof (NbFileSys_t));
    if (!fs)
    {
        NbObjDeRef (fsObj);
        return NULL;
    }
    NbObjSetData (fsObj, fs);
    NbObjInstallSvcs (fsObj, &fsSvcTab);
    // Initialize file system
    fs->files = ListCreate ("NbFile_t", true, offsetof (NbFile_t, obj));
    if (!fs->files)
    {
        NbObjDeRef (fsObj);
        free (fs);
        return false;
    }
    fs->internal = NULL;
    fs->type = vol->volFileSys;
    fs->volume = NbObjRef (volObj);
    fs->driver = fsTypeToDriver (fs->type);
    // Initialize FS driver
    if (!FsMount (fs->driver, fsObj))
    {
        NbObjDeRef (fsObj);
        ListDestroy (fs->files);
        free (fs);
        return false;
    }
    return fsObj;
}

static bool VfsFsOpenFile (void* obj, void* params)
{
    NbObject_t* fs = obj;
    NbOpenFileOp_t* op = params;
    NbFileSys_t* filesys = NbObjGetData (fs);
    NbFile_t* file = (NbFile_t*) malloc (sizeof (NbFile_t));
    if (!file)
        return false;
    ObjCreate ("NbFile_t", &file->obj);
    strcpy (file->name, op->name);
    file->fileSys = fs;
    file->pos = 0;
    // Call FS driver
    if (!FsOpenFile (filesys->driver, fs, file))
    {
        free (file);
        return false;
    }
    op->file = file;
    return true;
}

static bool VfsFsDumpData (void* objp, void* params)
{
    return true;
}

static bool VfsFsNotify (void* objp, void* params)
{
    return true;
}

static NbObjSvc fsSvcs[] =
    {NULL, NULL, NULL, VfsFsDumpData, VfsFsNotify, VfsFsOpenFile};

NbObjSvcTab_t fsSvcTab = {ARRAY_SIZE (fsSvcs), fsSvcs};

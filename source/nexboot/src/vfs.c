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

static int id = 0;

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
    file->fileSys = NbObjRef (fs);
    file->pos = 0;
    file->blockBuf = malloc (filesys->blockSz);
    if (!file->blockBuf)
    {
        free (file);
        return false;
    }
    file->fileId = ++id;
    // Call FS driver
    if (!FsOpenFile (filesys->driver, fs, file))
    {
        free (file->blockBuf);
        free (file);
        return false;
    }
    op->file = file;
    ListAddBack (filesys->files, file, file->fileId);
    return true;
}

static bool VfsFsCloseFile (void* obj, void* params)
{
    NbObject_t* fs = obj;
    NbFileSys_t* filesys = NbObjGetData (fs);
    NbFile_t* file = params;
    assert (file);
    FsCloseFile (filesys->driver, fs, file);
    NbObjDeRef (file->fileSys);
    ObjDestroy (&file->obj);
    ListRemove (filesys->files, ListFind (filesys->files, file->fileId));
    free (file->blockBuf);
    return true;
}

static bool VfsFsSeekFile (void* obj, void* params)
{
    NbObject_t* fs = obj;
    NbFileSys_t* filesys = NbObjGetData (fs);
    NbSeekOp_t* seek = params;
    assert (seek && seek->file);
    if (seek->relative)
        seek->file->pos += seek->pos;
    else
        seek->file->pos = seek->pos;
    if (seek->file->pos >= seek->file->size)
        return false;
    return true;
}

static bool VfsFsReadFile (void* obj, void* params)
{
    NbObject_t* fsObj = obj;
    NbFileSys_t* fs = fsObj->data;
    NbReadOp_t* op = params;
    assert (op->file && op->buf);
    void* buf = op->buf;
    op->bytesRead = 0;
    // Get number of blocks to read
    uint32_t numBlocks = (op->count + (fs->blockSz - 1)) / fs->blockSz;
    for (int i = 0; i < numBlocks; ++i)
    {
        // Read in block
        if (!FsReadBlock (fs->driver, fsObj, op->file, op->file->pos))
            return false;
        // Figure out number of bytes to copy
        uint32_t bytesRead = 0;
        uint32_t base = 0;
        if ((op->file->pos + fs->blockSz) > op->file->size)
            bytesRead = op->file->size - op->file->pos;
        else if ((op->bytesRead + fs->blockSz) > op->count)
            bytesRead = op->count - op->bytesRead;
        else
        {
            bytesRead = fs->blockSz;
            // Check if file position is block aligned, and correct it if not
            if (op->file->pos % fs->blockSz)
            {
                bytesRead -= op->file->pos % fs->blockSz;
                base += op->file->pos % fs->blockSz;
            }
        }
        // Copy them to buffer
        memcpy (buf, op->file->blockBuf + base, bytesRead);
        buf += bytesRead;
        op->bytesRead += bytesRead;
        op->file->pos += bytesRead;
    }
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

static NbObjSvc fsSvcs[] = {NULL,
                            NULL,
                            NULL,
                            VfsFsDumpData,
                            VfsFsNotify,
                            VfsFsOpenFile,
                            VfsFsCloseFile,
                            VfsFsReadFile,
                            VfsFsSeekFile};

NbObjSvcTab_t fsSvcTab = {ARRAY_SIZE (fsSvcs), fsSvcs};

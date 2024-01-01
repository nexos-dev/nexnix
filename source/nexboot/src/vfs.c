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

#define BUFMAX 128

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
        case VOLUME_FS_ISO9660:
            return FS_DRIVER_ISO9660;
        default:
            return 0xFF;
    }
}

// Mounts a filesystem. Takes a volume object as argument, and returns a filesystem
// object
NbObject_t* NbVfsMountFs (NbObject_t* volObj, const char* name)
{
    NbVolume_t* vol = NbObjGetData (volObj);
    // Create file system object
    if (!NbObjFind ("/Interfaces/FileSys"))
        NbObjCreate ("/Interfaces/FileSys", OBJ_TYPE_DIR, 0);
    char buf[BUFMAX];
    char buf2[BUFMAX];
    snprintf (buf, BUFMAX, "/Interfaces/FileSys/%s", name);
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
    if (fs->driver == 0xFF)
    {
        NbLogMessage ("nexboot: refusing to mount unrecognized volume %s",
                      NEXBOOT_LOGLEVEL_DEBUG,
                      NbObjGetPath (fs->volume, buf2, BUFMAX));
        NbObjDeRef (fs->volume);
        NbObjDeRef (fsObj);
        ListDestroy (fs->files);
        free (fs);
        return NULL;
    }
    // Initialize FS driver
    if (!FsMount (fs->driver, fsObj))
    {
        NbObjDeRef (fsObj);
        ListDestroy (fs->files);
        free (fs);
        return false;
    }
    NbLogMessage ("nexboot: mounted FS %s on volume %s\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  NbObjGetPath (fsObj, buf, BUFMAX),
                  NbObjGetPath (fs->volume, buf2, BUFMAX));
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
    free (file);
    return true;
}

static bool VfsFsGetFileInfo (void* obj, void* params)
{
    NbObject_t* fs = obj;
    NbFileSys_t* filesys = NbObjGetData (fs);
    NbFileInfo_t* out = params;
    out->fileSys = fs;
    // Call internal routine
    return FsGetFileInfo (filesys->driver, fs, out);
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
    // Check if this is even needed
    if (op->file->pos >= op->file->size)
    {
        op->bytesRead = 0;
        return true;
    }
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
        if ((op->file->pos + ((op->count > fs->blockSz) ? fs->blockSz : op->count)) >
            op->file->size)
        {
            bytesRead = op->file->size - op->file->pos;
        }
        else if (fs->blockSz > op->count)
            bytesRead = op->count;
        else
        {
            bytesRead = fs->blockSz;
            // Check if file position is block aligned, and correct it if not
            if (op->file->pos % fs->blockSz)
                bytesRead -= op->file->pos % fs->blockSz;
        }
        base += op->file->pos % fs->blockSz;
        // Copy them to buffer
        memcpy (buf, op->file->blockBuf + base, bytesRead);
        buf += bytesRead;
        op->bytesRead += bytesRead;
        op->file->pos += bytesRead;
        // If we didn't read in a full block, than don't go to next block
        if (bytesRead != fs->blockSz)
            break;
    }
    return true;
}

// Unmounts a volume
bool NbVfsUnmount (NbObject_t* fsObj)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    // Close all open files
    ListEntry_t* curFile = ListFront (fs->files);
    while (curFile)
    {
        NbFile_t* file = ListEntryData (curFile);
        VfsFsCloseFile (fsObj, file);
        curFile = ListIterate (curFile);
    }
    ListDestroy (fs->files);
    FsUnmount (fs->driver, fsObj);
    NbObjDeRef (fs->volume);
    NbObjDeRef (fsObj);
    char buf[BUFMAX];
    NbLogMessage ("nexboot: unmounted FS %s\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  NbObjGetPath (fsObj, buf, BUFMAX));
    return true;
}

static bool VfsFsGetDir (void* objp, void* params)
{
    NbObject_t* fsObj = objp;
    NbFileSys_t* fs = NbObjGetData (fsObj);
    NbGetDirOp_t* op = params;
    // Call FS
    return FsGetDir (fs->driver, fsObj, op->path, op->iter);
}

static bool VfsFsReadDir (void* objp, void* params)
{
    NbObject_t* fsObj = objp;
    NbFileSys_t* fs = NbObjGetData (fsObj);
    NbDirIter_t* iter = params;
    return FsReadDir (fs->driver, fsObj, iter);
}

// Filesystem name table
static const char* volFsNames[] = {"unknown", "fat12", "fat16", "fat32", "ext2", "fat", "iso9660"};

static bool VfsFsDumpData (void* objp, void* params)
{
    NbObject_t* vfsObj = objp;
    NbFileSys_t* vfs = NbObjGetData (vfsObj);
    void (*writeData) (const char* fmt, ...) = params;
    writeData ("Parent volume: %s\n", vfs->volume->name);
    NbVolume_t* vol = NbObjGetData (vfs->volume);
    writeData ("Parent disk: %s\n", vol->disk->name);
    writeData ("Filesystem type: %s\n", volFsNames[vfs->type]);
    writeData ("Block size: %u\n", vfs->blockSz);
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
                            VfsFsSeekFile,
                            VfsFsGetFileInfo,
                            VfsFsGetDir,
                            VfsFsReadDir};

NbObjSvcTab_t fsSvcTab = {ARRAY_SIZE (fsSvcs), fsSvcs};

// Wrapper functions

// Opens a file
NbFile_t* NbVfsOpenFile (NbObject_t* fs, const char* name)
{
    NbOpenFileOp_t op;
    op.name = name;
    if (!NbObjCallSvc (fs, NB_VFS_OPEN_FILE, &op))
        return NULL;
    return op.file;
}

// Closes a file
void NbVfsCloseFile (NbObject_t* fs, NbFile_t* file)
{
    NbObjCallSvc (fs, NB_VFS_CLOSE_FILE, file);
}

// Gets file info
bool NbVfsGetFileInfo (NbObject_t* fs, NbFileInfo_t* out)
{
    return NbObjCallSvc (fs, NB_VFS_GET_FILE_INFO, out);
}

// Seeks to position
bool NbVfsSeekFile (NbObject_t* fs, NbFile_t* file, uint32_t pos, bool relative)
{
    NbSeekOp_t op;
    op.file = file;
    op.pos = pos;
    op.relative = relative;
    return NbObjCallSvc (fs, NB_VFS_SEEK_FILE, &op);
}

// Reads from file
int32_t NbVfsReadFile (NbObject_t* fs, NbFile_t* file, void* buf, uint32_t count)
{
    NbReadOp_t op;
    op.buf = buf;
    op.count = count;
    op.file = file;
    if (!NbObjCallSvc (fs, NB_VFS_READ_FILE, &op))
        return -1;
    return op.bytesRead;
}

// Gets a directory
bool NbVfsGetDir (NbObject_t* fs, const char* dir, NbDirIter_t* iter)
{
    NbGetDirOp_t op = {0};
    op.iter = iter;
    op.path = dir;
    return NbObjCallSvc (fs, NB_VFS_GET_DIR, &op);
}

// Iterates a directory
bool NbVfsReadDir (NbObject_t* fs, NbDirIter_t* iter)
{
    return NbObjCallSvc (fs, NB_VFS_READ_DIR, iter);
}

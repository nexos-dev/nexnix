/*
    iso9660.c - contains ISO9660 driver
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

#include <assert.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/vfs.h>
#include <string.h>

typedef struct _isodir
{
    uint8_t recSize;       // Size of record
    uint8_t extAttrLen;    // Length of extended attributes
    uint32_t extentL;      // Location of extent
    uint32_t extentM;
    uint32_t lengthL;    // Length of extent
    uint32_t lengthM;
    uint8_t createTime[7];
    uint8_t flags;            // Flags for this record
    uint8_t fileUnitSz;       // Size of file unit
    uint8_t interleaveGap;    // Interleave gap
    uint16_t volumeL;         // Volume extent is recorded on
    uint16_t volumeM;
    uint8_t nameLen;    // Length of name
} __attribute__ ((packed)) IsoDirRecord_t;

#define ISO_DIRREC_NOEXISTS (1 << 0)
#define ISO_DIRREC_ISDIR    (1 << 1)
#define ISO_DIRREC_MULTIEXT (1 << 7)

// Volume descriptor header
typedef struct _voldesc
{
    uint8_t type;     // Type of volume descriptor
    uint8_t id[5];    // Contains "CD001"
    uint8_t ver;      // Version of descriptor data
} __attribute__ ((packed)) IsoVolDesc_t;

// PVD structure
typedef struct _pvd
{
    IsoVolDesc_t desc;
    uint8_t resvd1;
    uint8_t sysId[32];     // System ID
    uint8_t voldId[32];    // Volume ID
    uint64_t resvd2;
    uint32_t volSizeL;    // Size of volume
    uint32_t volSizeB;
    uint8_t resvd3[32];
    uint16_t volSetSzL;    // Size of volume set
    uint16_t volSetSzM;
    uint16_t volSeqL;    // Volume sequence number
    uint16_t volSeqM;
    uint16_t blockSzL;    // Size of logical block
    uint16_t blockSzM;
    uint32_t pathTableSzL;    // Size of path table
    uint32_t pathTableSzM;
    uint16_t lPathLocL;    // Location L-path table
    uint16_t lPathLocM;
    uint16_t optLPathLocL;
    uint16_t optLPathLocM;
    uint16_t mPathLocL;    // Location M-path table
    uint16_t mPathLocM;
    uint16_t optMPathLocL;
    uint16_t optMPathLocM;
    IsoDirRecord_t rootDir;    // Root directory record
    // Everything beyond here is unimportant and hence not listed here
} __attribute__ ((packed)) IsoPvd_t;

// Volume descriptor types
#define ISO_DESC_BOOT 0
#define ISO_DESC_PVD  1
#define ISO_DESC_SUP  2
#define ISO_DESC_PART 3
#define ISO_DESC_TERM 4

#define ISO_DESC_START 16

// Directory buffer
typedef struct _isodirbuf
{
    IsoDirRecord_t dir;    // Buffered entry
    uint8_t name[128];     // Name of file
    uint32_t parentExt;    // Parent's extent
    struct _isodirbuf* next;
} IsoDirBuffer_t;

#define ISO_BUF_NAMELEN 128

// Mount info
typedef struct _isomount
{
    uint16_t sectorSz;         // Sector size of volume
    uint16_t blockSz;          // Block size of volume
    IsoDirRecord_t rootDir;    // Root directory
    IsoDirRecord_t* curDir;    // Buffer for current directory
    IsoDirBuffer_t* dirBuf;    // Directory buffer
    uint32_t numBuffered;      // Number of buffered entries
} IsoMountInfo_t;

#define ISO_DIR_BUFFERED_MAX \
    256    // Max number of dir entries to be buffered at once

// File internal info
typedef struct _isofile
{
    uint32_t startBlock;    // Start block of file
} IsoFile_t;

// Directory iterator internal
typedef struct _isoiter
{
    IsoDirRecord_t* dir;    // Directory we are working in
    uint32_t block;         // Base block of directory
    uint32_t curPos;        // Current offset we are at
    uint32_t dirLen;        // Size of directory
} IsoDirIter_t;

// Pathname parser structure
typedef struct _pathpart
{
    const char* oldName;    // Original name
    char name[80];          // Component name
    bool isLastPart;        // Is this the last part?
} pathPart_t;

static void _parsePath (pathPart_t* part)
{
    memset (part->name, 0, 80);
    // Check if we need to skip over a '/'
    if (*part->oldName == '/')
        ++part->oldName;
    // Copy characters name until we reach a '/'
    int i = 0;
    bool hasExt = false;
    while (*part->oldName != '/' && *part->oldName != '\0')
    {
        if (*part->oldName == '.')
            hasExt = true;    // Take note
        part->name[i] = *part->oldName;
        ++i;
        ++part->oldName;
    }
    // If we reached a null terminator, finish
    if (*part->oldName == '\0')
        part->isLastPart = true;
}

// Strips version from file
static void isoStripVersion (char* path)
{
    while (*path != ';' && *path)
        ++path;
    *path = 0;
    // Check if there is a lone . at end
    if (*(path - 1) == '.')
        *(path - 1) = 0;    // Remove it
}

// Copies name from ISO directory entry to buffer, accounting for various gotchas
static void isoCopyName (IsoDirRecord_t* dir, char* nameOut)
{
    uint8_t* entryName = ((uint8_t*) dir) + sizeof (IsoDirRecord_t);
    if (dir->nameLen == 1 && *entryName == 0)
    {
        nameOut[0] = '.';
        nameOut[1] = 0;
    }
    else if (dir->nameLen == 1 && *entryName == 1)
    {
        nameOut[0] = '.';
        nameOut[1] = '.';
        nameOut[2] = 0;
    }
    else
    {
        memcpy (nameOut, ((void*) dir) + sizeof (IsoDirRecord_t), dir->nameLen);
        isoStripVersion (nameOut);
        // Strip version probably null terminated, but if there was no version, then
        // it didn't. Play it safe
        nameOut[dir->nameLen] = 0;
    }
}

// Reads a block from the volume
static bool isoReadBlock (NbFileSys_t* fs, void* buf, uint32_t block)
{
    IsoMountInfo_t* mountInfo = fs->internal;
    uint16_t blockSectors = mountInfo->blockSz;
    NbReadSector_t sector;
    sector.buf = buf;
    sector.count = blockSectors;
    sector.sector = block * blockSectors;
    return NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sector);
}

// Attempts to find buffered directory entry
IsoDirRecord_t* isoFindBuffer (IsoMountInfo_t* mountInfo,
                               uint32_t parentExt,
                               const char* name)
{
    IsoDirBuffer_t* curBuf = mountInfo->dirBuf;
    while (curBuf)
    {
        // Check parent extent and name
        if (parentExt == curBuf->parentExt &&
            !memcmp (curBuf->name, name, curBuf->dir.nameLen))
        {
            return &curBuf->dir;
        }
        curBuf = curBuf->next;
    }
    return NULL;
}

// Buffers a directory entry
static bool isoAddBuffer (IsoMountInfo_t* mountInfo,
                          IsoDirRecord_t* entry,
                          IsoDirRecord_t* parent)
{
    IsoDirBuffer_t* buf = (IsoDirBuffer_t*) calloc (sizeof (IsoDirBuffer_t), 1);
    if (!buf)
        return false;
    // Copy name, first doing bounds check
    if (entry->nameLen > ISO_BUF_NAMELEN)
    {
        // Skip
        free (buf);
        return true;
    }
    memcpy (&buf->dir, entry, sizeof (IsoDirRecord_t));
    // Name special cases: if name is '\0', actual name is '.'
    // If name is '\1', acutal name is '..'
    uint8_t* entryName = (uint8_t*) entry + sizeof (IsoDirRecord_t);
    if (*entryName == 0)
    {
        memcpy (buf->name, ".", 1);
        buf->dir.nameLen = 1;    // XXX, shouldn't edit directory entry
    }
    else if (*entryName == 1)
    {
        memcpy (buf->name, "..", 2);
        buf->dir.nameLen = 2;
    }
    memcpy (buf->name, (uint8_t*) entryName, entry->nameLen);
    buf->parentExt = parent->extentL;
    // Check if something needs to be evicted
    if (mountInfo->numBuffered == ISO_DIR_BUFFERED_MAX)
    {
        buf->next = mountInfo->dirBuf->next;
        free (mountInfo->dirBuf);
        mountInfo->dirBuf = buf;
    }
    else
    {
        // Add to list
        buf->next = mountInfo->dirBuf;
        mountInfo->dirBuf = buf;
        ++mountInfo->numBuffered;
    }
    return true;
}

// Checks if directory record is a showable file
static bool isoDirIsShowable (IsoDirRecord_t* dir)
{
    if (dir->flags & ISO_DIRREC_NOEXISTS)
        return false;
    return true;
}

// Finds entry in directory sector
static IsoDirRecord_t* isoFindInDir (NbFileSys_t* fs,
                                     void* buf,
                                     IsoDirRecord_t* parent,
                                     const char* name)
{
    IsoDirRecord_t* dir = buf;
    size_t nameLen = strlen (name);
    while (dir->recSize)
    {
        // Buffer entry
        if (!isoFindBuffer (fs->internal, parent->extentL, name))
        {
            if (!isoAddBuffer (fs->internal, dir, parent))
                return NULL;
        }
        // Compare names, accounting for '.' and '..' cases
        uint8_t* entryName = (uint8_t*) dir + sizeof (IsoDirRecord_t);
        if (*entryName == 0 && !strcmp (name, "."))
            return dir;
        else if (*entryName == 1 && !strcmp (name, ".."))
            return dir;
        else if (dir->nameLen && !memcmp (entryName, name, nameLen))
            return dir;
        buf += dir->recSize;
        dir = buf;
    }
    return NULL;
}

static IsoDirRecord_t* isoFindDir (NbFileSys_t* fs,
                                   IsoDirRecord_t* parent,
                                   const char* name)
{
    IsoDirRecord_t* rec = NULL;
    IsoMountInfo_t* mountInfo = fs->internal;
    // Search buffer for record first
    rec = isoFindBuffer (mountInfo, parent->extentL, name);
    if (rec)
        return rec;
    // Not in buffer, search parent's data area
    IsoDirRecord_t* dir = mountInfo->curDir;
    // Get size of data area
    uint32_t dataAreaSz = parent->lengthL / mountInfo->sectorSz;
    uint32_t block = parent->extentL;
    while (block < (parent->extentL + dataAreaSz))
    {
        // Read it in
        if (!isoReadBlock (fs, dir, block))
        {
            free (dir);
            return false;
        }
        // Go through directory and attempt to find record
        rec = isoFindInDir (fs, dir, parent, name);
        if (rec)
            return rec;
        ++block;
    }
    free (dir);
    return NULL;
}

#define ISO_SEARCH_FINISHED (void*) -1

static IsoDirRecord_t* isoDirNext (NbFileSys_t* fs,
                                   IsoDirIter_t* iter,
                                   char* nameOut)
{
    IsoMountInfo_t* mountInfo = fs->internal;
    // Get block and offset we are at
    uint32_t block = iter->block + (iter->curPos / fs->blockSz);
    uint32_t offset = iter->curPos % fs->blockSz;
    // Search
    IsoDirRecord_t* foundDir = NULL;
    void* curDirP = ((void*) iter->dir) + offset;
    IsoDirRecord_t* curDir = curDirP;
    while (!foundDir)
    {
        // Advance to next entry
        offset += curDir->recSize;
        curDirP += curDir->recSize;
        iter->curPos += curDir->recSize;
        curDir = curDirP;
        // Check if we need to read in another block
        if (offset >= fs->blockSz)
        {
            if (offset >= iter->dirLen)
                return ISO_SEARCH_FINISHED;    // We reached end
            if (!isoReadBlock (fs, iter->dir, ++block))
                return NULL;
            curDir = curDirP = iter->dir;
            offset = 0;
        }
        // Check for end
        if (curDir->recSize == 0)
            return ISO_SEARCH_FINISHED;
        if (isoDirIsShowable (curDir))
            foundDir = curDir;
    }
    // Copy name
    isoCopyName (curDir, nameOut);
    return foundDir;
}

bool IsoOpenFile (NbObject_t* fsObj, NbFile_t* file)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    IsoMountInfo_t* mountInfo = fs->internal;
    // Search directory hierarchy for file
    // Parse path into components
    pathPart_t part = {0};
    part.oldName = file->name;
    // Make search start at root
    IsoDirRecord_t* curDir = &mountInfo->rootDir;
    while (1)
    {
        _parsePath (&part);
        // Find this part
        curDir = isoFindDir (fs, curDir, part.name);
        if (!curDir)
            return false;
        if (part.isLastPart)
        {
            // Make sure this is a file we found
            if (curDir->flags & ISO_DIRREC_ISDIR)
                return false;
            break;
        }
        else
        {
            // Make sure this is a directory we found
            if (!(curDir->flags & ISO_DIRREC_ISDIR))
                return false;
        }
    }
    // Initialize structure
    IsoFile_t* intFile = (IsoFile_t*) malloc (sizeof (IsoFile_t));
    if (!intFile)
        return false;
    intFile->startBlock = curDir->extentL;
    file->internal = intFile;
    file->size = curDir->lengthL;
    return true;
}

bool IsoGetFileInfo (NbObject_t* fsObj, NbFileInfo_t* fileInf)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    IsoMountInfo_t* mountInfo = fs->internal;
    // Search directory hierarchy for file
    // Parse path into components
    pathPart_t part = {0};
    part.oldName = fileInf->name;
    // Make search start at root
    IsoDirRecord_t* curDir = &mountInfo->rootDir;
    while (1)
    {
        _parsePath (&part);
        // Find this part
        curDir = isoFindDir (fs, curDir, part.name);
        if (!curDir)
            return false;
        if (part.isLastPart)
        {
            // Make sure this is a file we found
            if (curDir->flags & ISO_DIRREC_ISDIR)
                return false;
            break;
        }
        else
        {
            // Make sure this is a directory we found
            if (!(curDir->flags & ISO_DIRREC_ISDIR))
                return false;
        }
    }
    fileInf->size = curDir->lengthL;
    if (curDir->flags & ISO_DIRREC_ISDIR)
        fileInf->type = NB_FILE_DIR;
    else
        fileInf->type = NB_FILE_FILE;
    return true;
}

bool IsoGetDir (NbObject_t* fsObj, const char* path, NbDirIter_t* iter)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    IsoMountInfo_t* mountInfo = fs->internal;
    // Parse path
    pathPart_t part = {0};
    part.oldName = path;
    // Start at root directory
    IsoDirRecord_t* curDir = &mountInfo->rootDir;
    while (!part.isLastPart)
    {
        _parsePath (&part);
        // Find this part
        curDir = isoFindDir (fs, curDir, part.name);
        if (!curDir)
            return false;
        // Make sure this is a directory we found
        if (!(curDir->flags & ISO_DIRREC_ISDIR))
            return false;
    }
    // Initialize internal iterator
    IsoDirIter_t* iterInt = (IsoDirIter_t*) &iter->internal;
    iterInt->curPos = 0;
    iterInt->block = curDir->extentL;
    iterInt->dirLen = curDir->lengthL;
    iterInt->dir = (IsoDirRecord_t*) malloc (curDir->lengthL);
    if (!iterInt->dir)
        return false;
    curDir = iterInt->dir;
    // Start directory read
    if (!isoReadBlock (fs, iterInt->dir, iterInt->block))
    {
        free (iterInt->dir);
        return false;
    }
    if (!curDir->recSize)
    {
        // Directory is empty
        iter->name[0] = 0;
        free (iterInt->dir);
        return true;
    }
    if (!isoDirIsShowable (curDir))
    {
        // Go to next showable entry
        curDir = isoDirNext (fs, iterInt, (char*) &iter->name);
        if (!curDir)
        {
            free (iterInt->dir);
            return false;
        }
        else if (curDir == ISO_SEARCH_FINISHED)
        {
            // Directory is empty
            iter->name[0] = 0;
            free (iterInt->dir);
            return true;
        }
    }
    else
    {
        // Copy name
        isoCopyName (curDir, iter->name);
    }
    // Set type
    if (curDir->flags & ISO_DIRREC_ISDIR)
        iter->type = NB_FILE_DIR;
    else
        iter->type = NB_FILE_FILE;
    return true;
}

bool IsoReadDir (NbObject_t* fsObj, NbDirIter_t* iter)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    IsoDirIter_t* iterInt = (IsoDirIter_t*) &iter->internal;
    // Find next entry
    IsoDirRecord_t* nextRec = isoDirNext (fs, iterInt, &iter->name);
    if (nextRec == ISO_SEARCH_FINISHED)
    {
        // Empty iterator
        iter->name[0] = 0;
        free (iterInt->dir);
        return true;
    }
    else if (!nextRec)
    {
        free (iterInt->dir);
        return false;
    }
    // Set type
    if (nextRec->flags & ISO_DIRREC_ISDIR)
        iter->type = NB_FILE_DIR;
    else
        iter->type = NB_FILE_FILE;
    return true;
}

bool IsoCloseFile (NbObject_t* fs, NbFile_t* file)
{
    free (file->internal);
    return true;
}

bool IsoReadFileBlock (NbObject_t* fsObj, NbFile_t* file, uint32_t pos)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    IsoFile_t* intFile = file->internal;
    // Convert to block number
    pos /= fs->blockSz;
    pos += intFile->startBlock;
    // Read it in
    return isoReadBlock (fs, file->blockBuf, pos);
}

bool IsoMountFs (NbObject_t* fsObj)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    NbObject_t* volObj = fs->volume;
    NbVolume_t* vol = NbObjGetData (volObj);
    NbDiskInfo_t* disk = NbObjGetData (vol->disk);
    // Read in first volume descriptor
    void* buf = malloc (disk->sectorSz);
    if (!buf)
        return false;
    NbReadSector_t sector;
    sector.buf = buf;
    sector.count = 1;
    sector.sector = ISO_DESC_START;
    IsoVolDesc_t* desc = buf;
    while (desc->type != ISO_DESC_PVD)
    {
        if (!NbObjCallSvc (volObj, NB_VOLUME_READ_SECTORS, &sector))
        {
            free (buf);
            return false;
        }
        if (desc->type == ISO_DESC_TERM)
        {
            // Error out
            free (buf);
            return false;
        }
        sector.sector++;
    }
    IsoMountInfo_t* mountInfo = (IsoMountInfo_t*) malloc (sizeof (IsoMountInfo_t));
    if (!mountInfo)
    {
        free (buf);
        return false;
    }
    // Allocate directory buffer
    mountInfo->curDir = malloc (disk->sectorSz);
    if (!mountInfo->curDir)
    {
        free (buf);
        free (mountInfo);
        return false;
    }
    mountInfo->dirBuf = NULL;
    // Set PVD fields in mount info
    IsoPvd_t* pvd = buf;
    mountInfo->blockSz = pvd->blockSzL / disk->sectorSz;
    fs->blockSz = pvd->blockSzL;
    mountInfo->sectorSz = disk->sectorSz;
    memcpy (&mountInfo->rootDir, &pvd->rootDir, sizeof (IsoDirRecord_t));
    fs->internal = mountInfo;
    free (buf);
    return true;
}

bool IsoUnmountFs (NbObject_t* fsObj)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    IsoMountInfo_t* mountInfo = fs->internal;
    IsoDirBuffer_t* curBuf = mountInfo->dirBuf;
    while (curBuf)
    {
        free (curBuf);
        curBuf = curBuf->next;
    }
    free (mountInfo->curDir);
    free (mountInfo);
    return true;
}

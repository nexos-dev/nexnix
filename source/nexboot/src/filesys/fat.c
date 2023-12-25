/*
    fat.c - contains FAT driver
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
#include <libnex/array.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/vfs.h>
#include <string.h>

// Valid characters for FAT file name
static uint8_t fatValidChars[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// FAT data structures
typedef struct bpb
{
    uint8_t oemName[8];    // Generally "MSWIN4.1"
    uint16_t bytesPerSector;
    uint8_t sectPerCluster;
    uint16_t resvdSectors;
    uint8_t numFats;
    uint16_t rootEntCount;    // Number of entries in root directory
    uint16_t totalSect16;     // 16 bit count of sectors
    uint8_t media;
    uint16_t fatSz16;    // 16 bit FAT size
    uint16_t sectorPerTrk;
    uint16_t numHeads;
    uint32_t hiddenSect;
    uint32_t totalSect32;    // 32 bit count of sectors, in case 16 bits is too
                             // little
} __attribute__ ((packed)) Bpb_t;

typedef struct _bpb16
{
    Bpb_t bpb;
    uint8_t driveNum;
    uint8_t resvd;
    uint8_t bootSig;    // Contains 0x29
    uint32_t volId;
    uint8_t volLabel[11];
    uint8_t fsType[8];    // Do not rely on thid field
} __attribute__ ((packed)) Bpb16_t;

typedef struct _bpb32
{
    Bpb_t bpb;
    uint32_t fatSz32;    // 32 bit FAT size
    uint16_t extFlags;
    uint16_t fsVer;
    uint32_t rootCluster;    // Root directory cluster
    uint16_t fsInfoCluster;
    uint16_t backupBootSect;    // Backup boot sector
    uint8_t resvd[12];
    uint8_t drvNum;
    uint8_t resvd1;
    uint8_t bootSig;    // Contains 0x29
    uint32_t voldId;
    uint8_t volLabel[11];
    uint8_t fsType[8];
} __attribute__ ((packed)) Bpb32_t;

typedef struct _mbrfat
{
    uint8_t jmp[3];    // Jump instruction at start of MBR
    Bpb16_t bpb;
    uint8_t bootstrap[448];    // Bootstrap code
    uint16_t bootSig;
} __attribute__ ((packed)) MbrFat_t;

typedef struct _mbrfat32
{
    uint8_t jmp[3];    // Jump instruction at start of MBR
    Bpb32_t bpb;
    uint8_t bootstrap[420];    // Bootstrap code
    uint16_t bootSig;
} __attribute__ ((packed)) MbrFat32_t;

#define MBR_BOOTSIG 0xAA55

// Directory entry
typedef struct _fatdir
{
    uint8_t name[11];    // File name in 8.3
    uint8_t attr;
    uint8_t winResvd;
    uint8_t creationMs;    // File creation time
    uint16_t creationTime;
    uint16_t creationDate;
    uint16_t accessDate;
    uint16_t clusterHigh;    // High 16 of first cluster
    uint16_t writeTime;      // Write time / date
    uint16_t writeDate;
    uint16_t clusterLow;
    uint32_t fileSz;
} __attribute__ ((packed)) FatDirEntry_t;

// Directory entry attributes
#define FAT_DIR_RO     (1 << 0)
#define FAT_DIR_HIDDEN (1 << 1)
#define FAT_DIR_SYSTEM (1 << 2)
#define FAT_DIR_VOL_ID (1 << 3)
#define FAT_DIR_IS_DIR (1 << 4)
#define FAT_DIR_LFN    (FAT_DIR_RO | FAT_DIR_HIDDEN | FAT_DIR_SYSTEM | FAT_DIR_VOL_ID)

// LFN structure
typedef struct _lfnentry
{
    uint8_t order;        // Index of LFN entry
    uint16_t name1[5];    // First 5 five characters
    uint8_t attr;         // Must be FAT_DIR_LFN
    uint8_t type;         // Unused
    uint8_t checksum;     // Checksum of short name
    uint16_t name2[6];    // Second six characters
    uint16_t unused;
    uint16_t name3[2];    // Third two characters
} __attribute__ ((packed)) FatLfnEntry_t;

#define FAT_LFN_IS_LAST 0x40
#define FAT_NAMELEN     256

// Buffered directory entry
typedef struct _dirbuf
{
    FatDirEntry_t dirEnt;         // Internal info
    uint8_t name[FAT_NAMELEN];    // Name of this entry
    uint32_t cluster;             // Cluster of parent directory
} FatDirBuffer_t;

#define FAT_DIRBUF_GROWSZ 64
#define FAT_DIRBUF_MAX    256

// Cached FAT sector
typedef struct _cacheEnt
{
    void* data;
    uint32_t sector;    // Sector to use as key
} FatCacheEnt_t;

#define FAT_FATCACHE_MAX    64
#define FAT_FATCACHE_GROWSZ 16

// Filesystem mount info
typedef struct _fatmount
{
    FatDirEntry_t* dir;         // Directory we are working on
    Array_t* fatCache;          // Cache of FAT structures
    Array_t* dirBuffer;         // Buffered directory entries
    uint64_t fatBase;           // Base of fat
    uint32_t fatSz;             // FAT size in sectors
    uint64_t dataBase;          // Base of data area
    uint32_t rootDir;           // Root directory cluster / sector base
    uint32_t rootDirSz;         // Size of root directory in sectors, 0 on FAT32 volumes
    uint16_t sectPerCluster;    // Sectors per cluster
    uint16_t sectorSz;          // Sector size
} FatMountInfo_t;

// File internal info structure
typedef struct _fatfile
{
    uint32_t lastReadCluster;    // Last cluster in this file that was read
    uint32_t lastReadPos;        // Last position that was read from in file
    uint32_t startCluster;       // Start cluster in file
} FatFile_t;

// Pathname parser structure
typedef struct _pathpart
{
    const char* oldName;       // Original name
    char name[FAT_NAMELEN];    // Component name
    bool isLastPart;           // Is this the last part?
} pathPart_t;

// Directory iterator internals
typedef struct _fatdiriter
{
    FatDirEntry_t* dir;     // Directory pointer
    uint32_t curCluster;    // Cluster last entry was on
    uint32_t curIdx;        // Current index in directory
} FatDirIter_t;

#define FAT_SEARCH_FINISHED (void*) -1

// Converts file name to 8.3
static void fileTo83 (const char* in, char* out)
{
    // Handle . and ..
    if (!strcmp (in, "."))
    {
        strcpy (out, ".          ");
        return;
    }
    else if (!strcmp (in, ".."))
    {
        strcpy (out, "..         ");
        return;
    }
    size_t i = 0;
    memset (out, ' ', 11);
    // Convert name part
    while (i < 8 && in[i] && in[i] != '.')
    {
        // Convert lowercase to uppercase
        if (in[i] >= 'a' && in[i] <= 'z')
            out[i] = in[i] - 32;
        else if (in[i] < 0x20 || !fatValidChars[in[i]])
            out[i] = '_';
        else
            out[i] = in[i];
        ++i;
    }
    if (in[i])
    {
        // Advance to last period in in
        int j = i;
        while (in[j] != '.')
            ++j;
        // Advance past period
        ++j;
        // Get extension
        i = 0;
        char ext[4] = {0};
        while (i < 3 && in[j])
        {
            // Convert lowercase to uppercase
            if (in[j] >= 'a' && in[j] <= 'z')
                ext[i] = in[j] - 32;
            else if (in[j] < 0x20 || !fatValidChars[in[j]])
                ext[i] = '_';
            else
                ext[i] = in[j];
            ++i;
            ++j;
        }
        // Find length of extension
        int extLen = strlen (ext);
        // Go to extension start in out
        char* extPtr = out + (11 - extLen);
        memcpy (extPtr, ext, extLen);
    }
    out[11] = 0;
}

// Converts 8.3 to regular name
static void file83ToName (const char* in, char* out)
{
    memset (out, 0, 12);
    // Handle dot and dot-dot
    if (!memcmp (in, "..", 2))
    {
        strcpy (out, "..");
        return;
    }
    else if (!memcmp (in, ".", 1))
    {
        strcpy (out, ".");
        return;
    }
    const char* oin = in;
    // Copy all of in until we reach a space
    while (*in != ' ')
    {
        *out = *in;
        ++in;
        ++out;
    }
    // Go to next character in in
    while (*in == ' ')
    {
        ++in;
        if ((in - oin) >= 11)
            return;    // Return, as there is no extension
    }
    // Add a period to out for extension
    *out = '.';
    ++out;
    // Copy any remaining characters
    memcpy (out, in, 11 - (in - oin));
    out += 11 - (in - oin);
    // Add a null terminator
    *out = 0;
}

static void _parsePath (pathPart_t* part)
{
    memset (part->name, 0, FAT_NAMELEN);
    // Check if we need to skip over a '/'
    if (*part->oldName == '/')
        ++part->oldName;
    // Copy characters name until we reach a '/'
    int i = 0;
    char name[FAT_NAMELEN] = {0};
    while (*part->oldName != '/' && *part->oldName != '\0')
    {
        name[i] = *part->oldName;
        ++i;
        ++part->oldName;
    }
    // Copy out
    strcpy (part->name, name);
    // Skip over any slash
    if (*part->oldName == '/')
        ++part->oldName;
    // Check if this is the end
    if (*part->oldName == '\0')
        part->isLastPart = true;
}

// Caches a FAT sector
static bool fatCacheSector (FatMountInfo_t* mountInfo, void* sector, uint32_t sectorIdx)
{
    // Get an entry from array
    FatCacheEnt_t* cacheEnt = NULL;
    if (mountInfo->fatCache->allocatedElems == mountInfo->fatCache->maxElems)
        cacheEnt = ArrayGetElement (mountInfo->fatCache, 0);
    else
    {
        // Allocate an element
        size_t pos = ArrayFindFreeElement (mountInfo->fatCache);
        if (pos == ARRAY_ERROR)
            return false;
        cacheEnt = ArrayGetElement (mountInfo->fatCache, pos);
    }
    // Cache it
    cacheEnt->sector = sectorIdx;
    cacheEnt->data = sector;
    return true;
}

// Finds a cached FAT sector
static void* fatFindCache (FatMountInfo_t* mountInfo, uint32_t sectorIdx)
{
    ArrayIter_t iterSt = {0};
    ArrayIter_t* iter = ArrayIterate (mountInfo->fatCache, &iterSt);
    while (iter)
    {
        // Check if this is a match
        FatCacheEnt_t* cache = iter->ptr;
        if (cache->sector == sectorIdx)
            return cache->data;
        iter = ArrayIterate (mountInfo->fatCache, iter);
    }
    return NULL;
}

// Routine to read a cluster
static bool fatReadCluster (NbFileSys_t* filesys, void* buf, uint32_t cluster)
{
    FatMountInfo_t* fs = filesys->internal;
    // Compute sector number from cluster
    uint32_t sectorNum = ((cluster - 2) * fs->sectPerCluster) + fs->dataBase;
    NbReadSector_t sector;
    sector.buf = buf;
    sector.count = fs->sectPerCluster;
    sector.sector = sectorNum;
    return NbObjCallSvc (filesys->volume, NB_VOLUME_READ_SECTORS, &sector);
}

// Reads next cluster in FAT
static uint32_t fatReadNextCluster (NbFileSys_t* fs, uint32_t cluster)
{
    FatMountInfo_t* mountInfo = fs->internal;
    uint32_t fatTabOffset = 0;
    // Compute offset in FAT table as whole
    if (fs->type == VOLUME_FS_FAT32)
        fatTabOffset = cluster * 4;
    else if (fs->type == VOLUME_FS_FAT16)
        fatTabOffset = cluster * 2;
    else if (fs->type == VOLUME_FS_FAT12)
        fatTabOffset = cluster + (cluster / 2);
    assert (fatTabOffset);
    // Compute FAT sector number and offset in sector
    uint64_t fatSector = mountInfo->fatBase + (fatTabOffset / mountInfo->sectorSz);
    uint32_t fatSectOff = fatTabOffset % mountInfo->sectorSz;
    // Find in FAT cache
    uint8_t* fat = fatFindCache (mountInfo, fatSector);
    if (!fat)
    {
        // Read sector
        fat = (uint8_t*) malloc (mountInfo->sectorSz * 2);
        NbReadSector_t sector;
        sector.buf = fat;
        sector.count = 1;
        sector.sector = fatSector;
        if (!NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sector))
            return UINT32_MAX;
        // Cache it
        if (!fatCacheSector (mountInfo, fat, fatSector))
            return UINT32_MAX;
    }
    // Read in value
    if (fs->type == VOLUME_FS_FAT32)
    {
        uint32_t* entry = (uint32_t*) (fat + fatSectOff);
        return *entry & 0x0FFFFFFF;
    }
    else if (fs->type == VOLUME_FS_FAT16)
    {
        uint16_t* entry = (uint16_t*) (fat + fatSectOff);
        return *entry;
    }
    else if (fs->type == VOLUME_FS_FAT12)
    {
        // This one is hard. Check if we need to read an additional sector in case
        // FAT spans sector
        // FIXME: we should reference cache here
        if (fatSectOff == (mountInfo->sectorSz - 1))
        {
            NbReadSector_t sector;
            sector.buf = fat + mountInfo->sectorSz;
            sector.count = 1;
            sector.sector = fatSector + 1;
            if (!NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sector))
                return UINT32_MAX;
        }
        uint16_t fatVal = *((uint16_t*) (fat + fatSectOff));
        if (cluster & 1)
        {
            // Cluster is odd
            fatVal >>= 4;
        }
        else
            fatVal &= 0x0FFF;
        return fatVal;
    }
    return UINT32_MAX;
}

// Checks if a cluster marks EOF
static bool fatIsClusterEof (NbFileSys_t* fs, uint32_t cluster)
{
    if (fs->type == VOLUME_FS_FAT12)
        return cluster >= 0x0FF8;
    else if (fs->type == VOLUME_FS_FAT16)
        return cluster >= 0xFFF8;
    else if (fs->type == VOLUME_FS_FAT32)
        return cluster >= 0x0FFFFFF8;
    assert (0);
}

// Checks if cluster is marked bad
static bool fatIsClusterBad (NbFileSys_t* fs, uint32_t cluster)
{
    if (fs->type == VOLUME_FS_FAT12)
        return cluster == 0x0FF7;
    else if (fs->type == VOLUME_FS_FAT16)
        return cluster == 0xFFF7;
    else if (fs->type == VOLUME_FS_FAT32)
        return cluster == 0x0FFFFFF7;
    assert (0);
}

// Follows cluster chain to offset in file
static uint32_t fatFollowClusterChain (NbFileSys_t* fs, NbFile_t* file, uint32_t clusterPos)
{
    FatFile_t* fileInt = file->internal;
    if (!fileInt->lastReadCluster)
        fileInt->lastReadCluster = fileInt->startCluster;
    uint32_t curPos = fileInt->lastReadPos;
    uint32_t cluster = fileInt->lastReadCluster;
    while (curPos < clusterPos)
    {
        // Read next cluster
        cluster = fatReadNextCluster (fs, cluster);
        if (fatIsClusterBad (fs, cluster))
            return UINT32_MAX;
        else if (fatIsClusterEof (fs, cluster))
            return UINT32_MAX;
        ++curPos;
    }
    return cluster;
}

// Places directory entry in directory buffer
static bool fatBufferDirEnt (FatMountInfo_t* mountInfo,
                             const char* name,
                             FatDirEntry_t* ent,
                             uint32_t cluster)
{
    FatDirBuffer_t* buf = NULL;
    // Check if we need to replace an entry
    if (mountInfo->dirBuffer->allocatedElems == mountInfo->dirBuffer->maxElems)
    {
        // Replace first element
        buf = ArrayGetElement (mountInfo->dirBuffer, 0);
    }
    else
    {
        // Get from array
        size_t pos = ArrayFindFreeElement (mountInfo->dirBuffer);
        if (pos == ARRAY_ERROR)
            return false;
        buf = ArrayGetElement (mountInfo->dirBuffer, pos);
    }
    // Write out fields
    size_t nameLen = strlen (name);
    memcpy (buf->name, name, nameLen);
    buf->name[nameLen] = 0;
    memcpy (&buf->dirEnt, ent, sizeof (FatDirEntry_t));
    buf->cluster = cluster;
    return true;
}

// Finds directory entry in buffer
static FatDirEntry_t* fatFindBufferDir (FatMountInfo_t* mountInfo,
                                        const char* name,
                                        uint32_t cluster)
{
    // Iterate through buffer
    ArrayIter_t iterSt = {0};
    ArrayIter_t* iter = ArrayIterate (mountInfo->dirBuffer, &iterSt);
    while (iter)
    {
        // Check if we match
        FatDirBuffer_t* buf = iter->ptr;
        if (buf->cluster == cluster && !memcmp (buf->name, name, 11))
            return &buf->dirEnt;    // We have a match
        iter = ArrayIterate (mountInfo->dirBuffer, iter);
    }
    return NULL;
}

// Checks if entry is valid file
static inline bool fatIsValidFile (FatDirEntry_t* dir)
{
    if (dir[0].name[0] == 0xE5 || dir[0].attr & FAT_DIR_HIDDEN || dir[0].attr & FAT_DIR_VOL_ID)
    {
        return false;
    }
    return true;
}

// Checks if entry is an LFN
static inline bool fatIsLfn (FatDirEntry_t* dir)
{
    if ((dir->attr & FAT_DIR_LFN) == FAT_DIR_LFN)
        return true;
    return false;
}

// Parses LFN
static int fatParseLfn (FatDirEntry_t* dir, char* lfnName)
{
    int i = 0;
    while (fatIsLfn (&dir[i]))
    {
        // Get sequence number
        FatLfnEntry_t* lfnEnt = (FatLfnEntry_t*) &dir[i];
        uint8_t order = lfnEnt->order & (FAT_LFN_IS_LAST - 1);
        order--;
        // Set appropriate character in name
        // NOTE: we currently truncate the character to 8 bits.
        // This is not the best way
        for (int j = 0; j < 5; ++j)
        {
            lfnName[(order * 13) + j] = (uint8_t) lfnEnt->name1[j];
        }
        for (int j = 5; j < 12; ++j)
        {
            lfnName[(order * 13) + j] = (uint8_t) lfnEnt->name2[j - 5];
        }
        for (int j = 12; j < 14; ++j)
        {
            lfnName[(order * 14) + j] = (uint8_t) lfnEnt->name3[j - 11];
        }
        ++i;
    }
    return i;
}

// Finds a directory entry in a read directory
static FatDirEntry_t* fatFindInDir (FatMountInfo_t* mountInfo,
                                    FatDirEntry_t* dir,
                                    const char* name,
                                    uint32_t cluster,
                                    uint32_t dirSz)
{
    int i = 0;
    size_t nameLen = strlen (name);
    // LFN parsing state
    char lfnName[FAT_NAMELEN] = {0};
    bool foundLfn = false;
    // Get 8.3 version of name
    char name83[12] = {0};
    fileTo83 (name, name83);
    while (dir[i].name[0] && (dirSz > (i * sizeof (FatDirEntry_t))))
    {
        // Check if this is an LFN
        if (fatIsLfn (&dir[i]))
        {
            foundLfn = true;
            i += fatParseLfn (&dir[i], lfnName);
            continue;
        }
        // Check if we found an LFN for this
        const char* validName = NULL;
        const char* nameToCheck = NULL;
        size_t len = 0;
        if (foundLfn)
        {
            nameToCheck = lfnName;
            validName = name;
        }
        else
        {
            nameToCheck = (const char*) &dir[i].name;
            validName = name83;
            len = 11;
        }
        // If this is a valid file, buffer it
        if (fatIsValidFile (&dir[i]))
        {
            if (!fatFindBufferDir (mountInfo, nameToCheck, cluster))
            {
                if (!fatBufferDirEnt (mountInfo, nameToCheck, &dir[i], cluster))
                    return NULL;
            }
        }
        // Compare names
        if (!foundLfn)
        {
            if (!memcmp (nameToCheck, validName, len))
                return &dir[i];
        }
        else
        {
            if (!strcmp (nameToCheck, validName))
                return &dir[i];
            foundLfn = false;    // Reset LFN flag
        }
        ++i;
    }
    return NULL;
}

// Finds an entry in any directory by cluster
static FatDirEntry_t* fatFindDirCluster (NbFileSys_t* fs, uint32_t cluster, const char* name)
{
    if (!cluster)
        return NULL;    // Empty directory
    FatMountInfo_t* mountInfo = fs->internal;
    uint32_t ocluster = cluster;    // Store first cluster of directory
    uint32_t clusterSz = mountInfo->sectPerCluster * mountInfo->sectorSz;
    FatDirEntry_t* dir = mountInfo->dir;
    FatDirEntry_t* ent = NULL;
    do
    {
        // Read in directory cluster
        if (!fatReadCluster (fs, dir, cluster))
            return NULL;
        // Find path in read cluster
        ent = fatFindInDir (mountInfo, dir, name, ocluster, clusterSz);
        // Read next cluster
        cluster = fatReadNextCluster (fs, cluster);
        if (cluster == UINT32_MAX)
            return NULL;
        else if (fatIsClusterEof (fs, cluster))
            break;    // This is the end
        else if (fatIsClusterBad (fs, cluster))
            break;
    } while (!ent);
    return ent;
}

// Finds an entry in the root directory
static FatDirEntry_t* fatFindRootDir (NbFileSys_t* fs, const char* name)
{
    FatMountInfo_t* mountInfo = fs->internal;
    if (fs->type == VOLUME_FS_FAT32)
    {
        // Check buffer first
        FatDirEntry_t* ent = fatFindBufferDir (mountInfo, name, mountInfo->rootDir);
        if (ent)
            return ent;
        return fatFindDirCluster (fs, mountInfo->rootDir, name);
    }
    else
    {
        // Check buffer first
        FatDirEntry_t* bufEnt = fatFindBufferDir (mountInfo, name, 0);
        if (bufEnt)
            return bufEnt;
        // Get root directory base
        uint32_t rootDir = mountInfo->rootDir;
        FatDirEntry_t* dir = mountInfo->dir;
        FatDirEntry_t* ent = NULL;
        NbReadSector_t sector;
        sector.count = 1;
        sector.buf = dir;
        sector.sector = rootDir;
        do
        {
            if (!NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sector))
                return NULL;
            // Find path in read sector
            ent = fatFindInDir (mountInfo, dir, name, 0, mountInfo->sectorSz);
            ++sector.sector;
            // Check if we are at the end
            if ((sector.sector - rootDir) == mountInfo->rootDirSz)
                break;
        } while (!ent);
        return ent;
    }
}

static FatDirEntry_t* fatFindDir (NbFileSys_t* fs, FatDirEntry_t* parent, const char* name)
{
    uint32_t cluster = parent->clusterLow | (parent->clusterHigh << 16);
    // If cluster equals 0, than this is root directory. This case can happen with ..
    if (!cluster)
        return fatFindRootDir (fs, name);
    // Attempt to find in buffer
    FatDirEntry_t* ent = fatFindBufferDir (fs->internal, name, cluster);
    if (ent)
        return ent;
    return fatFindDirCluster (fs, cluster, name);
}

// Reads next directory entry after dirIdx in FAT12/FAT16 root directory
// Assumes dir is base of current sector dirIdx is in
static FatDirEntry_t* fatNextEntryRootDir (NbFileSys_t* fs,
                                           FatDirEntry_t* dir,
                                           uint32_t* dirIdx,
                                           char* nameOut)
{
    FatMountInfo_t* mountInfo = fs->internal;
    // Get entries in sector
    uint32_t entInSect = mountInfo->sectorSz / sizeof (FatDirEntry_t);
    // Get base sector and offset to read from in dir
    uint32_t sector = mountInfo->rootDir + ((*dirIdx) / entInSect);
    uint32_t offset = (*dirIdx) % entInSect;
    FatDirEntry_t* foundDir = NULL;
    while (!foundDir)
    {
        *dirIdx += 1;
        ++offset;
        // Check if we need to read in another sector
        if (offset >= entInSect)
        {
            // Read it in
            NbReadSector_t sect;
            sect.buf = dir;
            sect.count = 1;
            sect.sector = ++sector;
            if (!NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sect))
                return NULL;
            offset = 0;
        }
        // Check if entry is valid
        if (dir[offset].name[0] == 0)
            return FAT_SEARCH_FINISHED;    // End of directory
        // Handle LFNs
        bool foundLfn = false;
        if (fatIsLfn (&dir[offset]))
        {
            // FIXME: If LFN spans cluster, this will fail
            int res = fatParseLfn (&dir[offset], nameOut);
            offset += res;
            *dirIdx += res;
            foundLfn = true;
        }
        if (fatIsValidFile (&dir[offset]))
        {
            foundDir = &dir[offset];    // Entry found
            // Convert name away from 8.3 if needed
            if (!foundLfn)
                file83ToName ((const char*) &dir[offset].name, nameOut);
        }
    }
    return foundDir;
}

static FatDirEntry_t* fatNextEntry (NbFileSys_t* fs,
                                    FatDirEntry_t* dir,
                                    uint32_t* dirIdx,
                                    uint32_t* cluster,
                                    char* nameOut)
{
    // Check if we are examining root directory on FAT12/FAT16 volume
    if (!(*cluster))
        return fatNextEntryRootDir (fs, dir, dirIdx, nameOut);
    // Get entries in cluster
    uint32_t entInCluster = fs->blockSz / sizeof (FatDirEntry_t);
    // Get offset in cluster
    uint32_t offset = (*dirIdx) % entInCluster;
    // Search in dir
    FatDirEntry_t* foundDir = NULL;
    while (!foundDir)
    {
        *dirIdx += 1;
        ++offset;
        // Check if we need to read in another cluster
        if (offset >= entInCluster)
        {
            // Read it in
            *cluster = fatReadNextCluster (fs, *cluster);
            // Check for bad cluster and EOF
            if (fatIsClusterBad (fs, *cluster))
                return NULL;
            else if (fatIsClusterEof (fs, *cluster))
                return FAT_SEARCH_FINISHED;
            if (!fatReadCluster (fs, dir, *cluster))
                return NULL;
            offset = 0;
        }
        // Check if entry is valid
        if (dir[offset].name[0] == 0)
            return FAT_SEARCH_FINISHED;    // End of directory
        // Handle LFNs
        bool foundLfn = false;
        if (fatIsLfn (&dir[offset]))
        {
            // FIXME: If LFN spans cluster, this will fail
            int res = fatParseLfn (&dir[offset], nameOut);
            offset += res;
            *dirIdx += res;
            foundLfn = true;
        }
        if (fatIsValidFile (&dir[offset]))
        {
            foundDir = &dir[offset];    // Entry found
            // Convert name away from 8.3 if needed
            if (!foundLfn)
                file83ToName ((const char*) &dir[offset].name, nameOut);
        }
    }
    return foundDir;
}

// Reads in first part of directory
static FatDirEntry_t* fatStartReadDir (NbFileSys_t* fs, uint32_t* dirCluster)
{
    FatMountInfo_t* mountInfo = fs->internal;
    // Allocate directory space
    FatDirEntry_t* dir = (FatDirEntry_t*) malloc (fs->blockSz);
    if (!dir)
        return NULL;
    if (!(*dirCluster))
    {
        if (fs->type != VOLUME_FS_FAT32)
        {
            // Read root directory, FAT12 / FAT16 style
            NbReadSector_t sector;
            sector.buf = dir;
            sector.count = 1;
            sector.sector = mountInfo->rootDir;
            if (!NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sector))
                return NULL;
            return dir;
        }
        else
        {
            // Set dirCluster to root directory
            *dirCluster = mountInfo->rootDir;
        }
    }
    // Read in cluster
    if (!fatReadCluster (fs, dir, *dirCluster))
        return NULL;
    return dir;
}

bool FatOpenFile (NbObject_t* fsObj, NbFile_t* file)
{
    const char* name = file->name;
    NbFileSys_t* fs = NbObjGetData (fsObj);
    // Parse path into components
    pathPart_t part;
    memset (&part, 0, sizeof (pathPart_t));
    part.oldName = name;
    FatDirEntry_t* curDir = NULL;
    while (1)
    {
        _parsePath (&part);
        // Find this part. If curDir is NULL, use root directory
        if (!curDir)
            curDir = fatFindRootDir (fs, part.name);
        else
            curDir = fatFindDir (fs, curDir, part.name);
        if (!curDir)
            return false;
        if (part.isLastPart)
        {
            // Make sure this is a file we found
            if (curDir->attr & FAT_DIR_IS_DIR)
                return false;
            break;
        }
        else
        {
            // Make sure this is a directory we found
            if (!(curDir->attr & FAT_DIR_IS_DIR))
                return false;
        }
    }
    FatDirEntry_t* foundFile = curDir;
    // Initialize internal file structure
    file->internal = malloc (sizeof (FatFile_t));
    memset (file->internal, 0, sizeof (FatFile_t));
    FatFile_t* intFile = file->internal;
    intFile->startCluster = foundFile->clusterLow | (foundFile->clusterHigh << 16);
    file->size = foundFile->fileSz;
    return true;
}

bool FatCloseFile (NbObject_t* fsObj, NbFile_t* file)
{
    free (file->internal);
    return true;
}

bool FatGetFileInfo (NbObject_t* fsObj, NbFileInfo_t* fileInf)
{
    const char* name = fileInf->name;
    NbFileSys_t* fs = NbObjGetData (fsObj);
    // Parse path into components
    pathPart_t part;
    memset (&part, 0, sizeof (pathPart_t));
    part.oldName = name;
    FatDirEntry_t* curDir = NULL;
    while (1)
    {
        _parsePath (&part);
        // Find this part. If curDir is NULL, use root directory
        if (!curDir)
            curDir = fatFindRootDir (fs, part.name);
        else
            curDir = fatFindDir (fs, curDir, part.name);
        if (!curDir)
            return false;
        if (part.isLastPart)
            break;
        else
        {
            // Make sure this is a directory we found
            if (!(curDir->attr & FAT_DIR_IS_DIR))
                return false;
        }
    }
    // Set output
    fileInf->size = curDir->fileSz;
    if (curDir->attr & FAT_DIR_IS_DIR)
        fileInf->type = NB_FILE_DIR;
    else
        fileInf->type = NB_FILE_FILE;
    return true;
}

bool FatGetDir (NbObject_t* fsObj, const char* path, NbDirIter_t* iter)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    FatMountInfo_t* mountInfo = fs->internal;
    // Parse path into components
    pathPart_t part = {0};
    part.oldName = path;
    FatDirEntry_t* curDir = NULL;
    // Cluster directory is on
    uint32_t dirCluster = 0;
    // Loop through each component until we find correct one
    while (!part.isLastPart)
    {
        _parsePath (&part);
        // Find this part. If curDir is NULL, use root directory
        if (!curDir)
        {
            // If this is true and part is empty, just get the root directory
            // base
            if (part.isLastPart && !part.name[0])
            {
                // On FAT32, set dirCluster
                if (fs->type == VOLUME_FS_FAT32)
                    dirCluster = mountInfo->rootDir;
                break;
            }
            else
                curDir = fatFindRootDir (fs, part.name);
        }
        else
        {
            curDir = fatFindDir (fs, curDir, part.name);
        }
        if (!curDir)
            return false;
        dirCluster = (curDir->clusterHigh << 16) | curDir->clusterLow;
        // Make sure this is a directory we found
        if (!(curDir->attr & FAT_DIR_IS_DIR))
            return false;
    }
    // We have the directory, fill out first entry
    // Initialize iterator internal data
    FatDirIter_t* iterInt = (FatDirIter_t*) &iter->internal;
    iterInt->curCluster = dirCluster;
    iterInt->curIdx = 0;
    // Read in directory first sector / cluster
    FatDirEntry_t* dir = fatStartReadDir (fs, &dirCluster);
    if (!dir)
        return false;
    iterInt->dir = dir;
    // First check if directory is empty
    if (!dir[0].name[0])
    {
        // This directory is empty, fill out emptry iterator
        iter->name[0] = 0;
        free (iterInt->dir);
        return true;
    }
    // Check if this directory entry is actually a file
    if (fatIsLfn (dir))
    {
        int res = fatParseLfn (dir, iter->name);
        iterInt->curIdx += res;
        dir += res;
    }
    else if (!fatIsValidFile (dir))
    {
        // Go to next valid directory entry
        dir = fatNextEntry (fs, dir, &iterInt->curIdx, &iterInt->curCluster, iter->name);
        if (dir == FAT_SEARCH_FINISHED)
        {
            // This directory is empty, fill out emptry iterator
            iter->name[0] = 0;
            free (iterInt->dir);
            return true;
        }
        else if (!dir)
            return false;    // Error occurred
    }
    else
    {
        // Convert 8.3 to normal name
        file83ToName ((const char*) dir->name, iter->name);
    }
    // Set type
    if (dir->attr & FAT_DIR_IS_DIR)
        iter->type = NB_FILE_DIR;
    else
        iter->type = NB_FILE_FILE;
    return true;
}

bool FatReadDir (NbObject_t* fsObj, NbDirIter_t* iter)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    FatDirIter_t* intIter = (FatDirIter_t*) &iter->internal;
    // Grab directory
    FatDirEntry_t* dir = intIter->dir;
    // Get next entry
    FatDirEntry_t* nextEnt =
        fatNextEntry (fs, dir, &intIter->curIdx, &intIter->curCluster, iter->name);
    // Check if it's valid
    if (!nextEnt)
        return NULL;
    else if (nextEnt == FAT_SEARCH_FINISHED)
    {
        // Let caller know we finsihed
        iter->name[0] = 0;
        free (dir);    // Ensure directory is freed
    }
    else
    {
        // Set type
        if (nextEnt->attr & FAT_DIR_IS_DIR)
            iter->type = NB_FILE_DIR;
        else
            iter->type = NB_FILE_FILE;
    }
    return true;
}

bool FatReadFileBlock (NbObject_t* fsObj, NbFile_t* file, uint32_t pos)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    FatMountInfo_t* mountInfo = fs->internal;
    FatFile_t* fileInt = file->internal;
    // Convert pos to a file cluster index
    uint32_t fileClusterNum = pos / fs->blockSz;
    // Now we need to find the actual cluster number behind the relative cluster
    // number. We could parse the FAT from the beginning, but that's REALLY slow
    // Instead, we mantain a hint of where the last read cluster was and base it
    // off of that
    if (fileClusterNum == fileInt->lastReadPos && fileInt->lastReadCluster)
    {
        // Read in that cluster
        return fatReadCluster (fs, file->blockBuf, fileInt->lastReadCluster);
    }
    else if (fileClusterNum <= fileInt->lastReadPos)
    {
        fileInt->lastReadPos = 0;
        fileInt->lastReadCluster = 0;
    }
    // Follow cluster chain to fileClusterNum
    uint32_t cluster = fatFollowClusterChain (fs, file, fileClusterNum);
    if (cluster == UINT32_MAX)
        return false;
    fileInt->lastReadPos = fileClusterNum;    // Go to fileClusterNum
    fileInt->lastReadCluster = cluster;
    // Read in that cluster
    return fatReadCluster (fs, file->blockBuf, cluster);
}

bool FatMountFs (NbObject_t* fsObj)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    NbObject_t* volObj = fs->volume;
    NbVolume_t* vol = NbObjGetData (volObj);
    NbDiskInfo_t* disk = NbObjGetData (vol->disk);
    FatMountInfo_t* mountInfo = (FatMountInfo_t*) malloc (sizeof (FatMountInfo_t));
    if (!mountInfo)
        return false;
    void* mbrData = (void*) malloc (disk->sectorSz);
    if (!mbrData)
    {
        free (mountInfo);
        return false;
    }
    // Read in MBR
    NbReadSector_t sector;
    sector.buf = mbrData;
    sector.count = 1;
    sector.sector = 0;
    if (!NbObjCallSvc (volObj, NB_VOLUME_READ_SECTORS, &sector))
    {
        free (mbrData);
        free (mountInfo);
        return false;
    }
    // If FAT variant is unknown, set it
    if (fs->type == VOLUME_FS_FAT)
    {
        // Get BPB
        Bpb_t* bpb = (mbrData + 3);
        // The easy way to check if this is FAT32 is to check if rootEntCount = 0
        if (!bpb->rootEntCount)
        {
            // This is FAT32
            vol->volFileSys = VOLUME_FS_FAT32;
            fs->type = VOLUME_FS_FAT32;
        }
        else
        {
            // Follow MS's algorithm to get FAT type
            uint32_t fatSize = 0, sectorCount = 0;
            if (bpb->fatSz16)
                fatSize = bpb->fatSz16;
            else
                assert (0);
            if (bpb->totalSect16)
                sectorCount = bpb->totalSect16;
            else
                sectorCount = bpb->totalSect32;
            // Find number of root directory sectors
            uint32_t rootDirSects =
                ((bpb->rootEntCount * sizeof (FatDirEntry_t)) + (disk->sectorSz - 1)) /
                disk->sectorSz;
            // Find number of data sectors
            uint32_t dataSectors =
                sectorCount - (bpb->resvdSectors + (bpb->numFats * fatSize) + rootDirSects);
            uint32_t clusterCount = dataSectors / bpb->sectPerCluster;
            if (clusterCount < 4085)
            {
                // This is FAT12
                vol->volFileSys = VOLUME_FS_FAT12;
                fs->type = VOLUME_FS_FAT12;
            }
            else if (clusterCount < 65525)
            {
                // This is FAT16
                vol->volFileSys = VOLUME_FS_FAT16;
                fs->type = VOLUME_FS_FAT16;
            }
        }
    }
    // Now we split depending on wheter this is a FAT 12 / FAT 16 or FAT 32
    // volume
    if (fs->type == VOLUME_FS_FAT32)
    {
        MbrFat32_t* mbr = mbrData;
        // Do some checks
        if (mbr->bootSig != MBR_BOOTSIG)
        {
            free (mbr);
            free (mountInfo);
            return false;
        }
        // Grab the info we need and copy it to mount info
        memset (mountInfo, 0, sizeof (FatMountInfo_t));
        mountInfo->sectorSz = disk->sectorSz;
        mountInfo->rootDir = mbr->bpb.rootCluster;
        mountInfo->rootDirSz = 0;
        mountInfo->fatBase = mbr->bpb.bpb.resvdSectors;
        mountInfo->fatSz = mbr->bpb.fatSz32;
        mountInfo->sectPerCluster = mbr->bpb.bpb.sectPerCluster;
        mountInfo->dataBase = mountInfo->fatBase + (mbr->bpb.bpb.numFats * mountInfo->fatSz);
    }
    else
    {
        MbrFat_t* mbr = mbrData;
        // Do some checks
        if (mbr->bootSig != MBR_BOOTSIG)
        {
            free (mbr);
            free (mountInfo);
            return false;
        }
        // Grab the info we need and copy it to mount info
        memset (mountInfo, 0, sizeof (FatMountInfo_t));
        mountInfo->sectorSz = disk->sectorSz;
        mountInfo->fatBase = mbr->bpb.bpb.resvdSectors;
        mountInfo->fatSz = mbr->bpb.bpb.fatSz16;
        mountInfo->sectPerCluster = mbr->bpb.bpb.sectPerCluster;
        mountInfo->rootDir = mountInfo->fatBase + (mountInfo->fatSz * mbr->bpb.bpb.numFats);
        mountInfo->rootDirSz =
            ((mbr->bpb.bpb.rootEntCount * sizeof (FatDirEntry_t)) + (disk->sectorSz - 1)) /
            disk->sectorSz;
        mountInfo->dataBase =
            mountInfo->fatBase + (mbr->bpb.bpb.numFats * mountInfo->fatSz) + mountInfo->rootDirSz;
    }
    free (mbrData);
    fs->blockSz = disk->sectorSz * mountInfo->sectPerCluster;
    // Initialize caches
    uint32_t sz = mountInfo->sectPerCluster * mountInfo->sectorSz;
    mountInfo->dir = malloc (sz);
    if (!mountInfo->dir)
    {
        free (mountInfo);
        return false;
    }
    mountInfo->fatCache =
        ArrayCreate (FAT_FATCACHE_GROWSZ, FAT_FATCACHE_MAX, sizeof (FatCacheEnt_t));
    if (!mountInfo->fatCache)
    {
        free (mountInfo);
        return false;
    }
    // Initialize directory buffer
    mountInfo->dirBuffer = ArrayCreate (FAT_DIRBUF_GROWSZ, FAT_DIRBUF_MAX, sizeof (FatDirBuffer_t));
    if (!mountInfo->dirBuffer)
    {
        free (mountInfo->dir);
        ArrayDestroy (mountInfo->fatCache);
        free (mountInfo);
    }
    fs->internal = mountInfo;
    return true;
}

bool FatUnmountFs (NbObject_t* fsObj)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    FatMountInfo_t* mountInfo = fs->internal;
    ArrayDestroy (mountInfo->dirBuffer);
    ArrayDestroy (mountInfo->fatCache);
    free (mountInfo->dir);
    free (mountInfo);
    return true;
}

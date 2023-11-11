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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/vfs.h>
#include <string.h>

// Valid characters for FAT file name
static uint8_t fatValidChars[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

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

// Cached data structure
typedef struct _cacheEnt
{
    void* data;
    int block;
} FatCacheEnt_t;

// Filesystem mount info
typedef struct _fatmount
{
    FatCacheEnt_t cachedDir;    // Cached directory cluster
    FatCacheEnt_t cachedFat;    // Cached FAT sector
    uint64_t fatBase;           // Base of fat
    uint32_t fatSz;             // FAT size in sectors
    uint64_t dataBase;          // Base of data area
    uint32_t rootDir;           // Root directory cluster / sector base
    uint32_t rootDirSz;    // Size of root directory in sectors, 0 on FAT32 volumes
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
    const char* oldName;    // Original name
    char name[80];          // Component name
    bool isLastPart;        // Is this the last part?
} pathPart_t;

// Converts file name to 8.3
static void fileTo83 (const char* in, char* out)
{
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

static void _parsePath (pathPart_t* part)
{
    memset (part->name, 0, 80);
    // Check if we need to skip over a '/'
    if (*part->oldName == '/')
        ++part->oldName;
    // Copy characters name until we reach a '/'
    int i = 0;
    char name[80] = {0};
    while (*part->oldName != '/' && *part->oldName != '\0')
    {
        name[i] = *part->oldName;
        ++i;
        ++part->oldName;
    }
    // Convert to 8.3
    fileTo83 (name, part->name);
    // If we reached a null terminator, finish
    if (*part->oldName == '\0')
        part->isLastPart = true;
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
// NOTE: This routine is PAINFULLY slow. We need to cache parts of the FAT table
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
    // Read sector
    uint8_t* fat = mountInfo->cachedFat.data;
    NbReadSector_t sector;
    sector.buf = fat;
    sector.count = 1;
    sector.sector = fatSector;
    if (!NbObjCallSvc (fs->volume, NB_VOLUME_READ_SECTORS, &sector))
        return UINT32_MAX;
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
static uint32_t fatFollowClusterChain (NbFileSys_t* fs,
                                       NbFile_t* file,
                                       uint32_t clusterPos)
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

// Finds a directory entry in a read directory
static FatDirEntry_t* fatFindInDir (FatDirEntry_t* dir,
                                    const char* name,
                                    uint32_t dirSz)
{
    int i = 0;
    while (dir[i].name[0] && (dirSz > (i * sizeof (FatDirEntry_t))))
    {
        if (!memcmp (dir[i].name, name, 11))
            return &dir[i];
        ++i;
    }
    return NULL;
}

// Finds an entry in any directory by cluster
static FatDirEntry_t* fatFindDirCluster (NbFileSys_t* fs,
                                         uint32_t cluster,
                                         const char* name)
{
    if (!cluster)
        return NULL;    // Empty directory
    FatMountInfo_t* mountInf = fs->internal;
    uint32_t clusterSz = mountInf->sectPerCluster * mountInf->sectorSz;
    FatDirEntry_t* dir = mountInf->cachedDir.data;
    FatDirEntry_t* ent = NULL;
    do
    {
        // Read in directory cluster
        if (!fatReadCluster (fs, dir, cluster))
            return NULL;
        // Find path in read cluster
        ent = fatFindInDir (dir, name, clusterSz);
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
    FatMountInfo_t* mountInf = fs->internal;
    if (fs->type == VOLUME_FS_FAT32)
        return fatFindDirCluster (fs, mountInf->rootDir, name);
    else
    {
        // Get root directory base
        uint32_t rootDir = mountInf->rootDir;
        FatDirEntry_t* dir = mountInf->cachedDir.data;
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
            ent = fatFindInDir (dir, name, mountInf->sectorSz);
            ++sector.sector;
            // Check if we are at the end
            if ((sector.sector - rootDir) == mountInf->rootDirSz)
                break;
        } while (!ent);
        return ent;
    }
}

static FatDirEntry_t* fatFindDir (NbFileSys_t* fs,
                                  FatDirEntry_t* parent,
                                  const char* name)
{
    uint32_t cluster = parent->clusterLow | (parent->clusterHigh << 16);
    return fatFindDirCluster (fs, cluster, name);
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
            break;
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

bool FatReadFileBlock (NbObject_t* fsObj, NbFile_t* file, uint32_t pos)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    FatMountInfo_t* mountInf = fs->internal;
    FatFile_t* fileInt = file->internal;
    // Convert pos to a file cluster index
    uint32_t fileClusterNum = pos / fs->blockSz;
    // Now we need to find the actual cluster number behind the relative cluster
    // number. We could parse the FAT from the beginning, but that's REALLY slow
    // Instead, we mantain a hint of where the last read was and base it off of that
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
            uint32_t rootDirSects = ((bpb->rootEntCount * sizeof (FatDirEntry_t)) +
                                     (disk->sectorSz - 1)) /
                                    disk->sectorSz;
            // Find number of data sectors
            uint32_t dataSectors =
                sectorCount -
                (bpb->resvdSectors + (bpb->numFats * fatSize) + rootDirSects);
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
    // Now we split depending on wheter this is a FAT 12 / FAT 16 or FAT 32 volume
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
        mountInfo->dataBase =
            mountInfo->fatBase + (mbr->bpb.bpb.numFats * mountInfo->fatSz);
        fs->blockSz = disk->sectorSz * mountInfo->sectPerCluster;
        // Initialize caches
        uint32_t sz = mountInfo->sectPerCluster * mountInfo->sectorSz;
        mountInfo->cachedDir.data = malloc (sz);
        if (!mountInfo->cachedDir.data)
        {
            free (mbr);
            free (mountInfo);
            return false;
        }
        mountInfo->cachedFat.data = malloc (disk->sectorSz);
        if (!mountInfo->cachedFat.data)
        {
            free (mbr);
            free (mountInfo);
            return false;
        }
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
        mountInfo->rootDir =
            mountInfo->fatBase + (mountInfo->fatSz * mbr->bpb.bpb.numFats);
        mountInfo->rootDirSz =
            ((mbr->bpb.bpb.rootEntCount * sizeof (FatDirEntry_t)) +
             (disk->sectorSz - 1)) /
            disk->sectorSz;
        mountInfo->dataBase = mountInfo->fatBase +
                              (mbr->bpb.bpb.numFats * mountInfo->fatSz) +
                              mountInfo->rootDirSz;
        fs->blockSz = disk->sectorSz * mountInfo->sectPerCluster;
        // Initialize caches
        uint32_t sz = mountInfo->sectPerCluster * mountInfo->sectorSz;
        mountInfo->cachedDir.data = malloc (sz);
        if (!mountInfo->cachedDir.data)
        {
            free (mbr);
            free (mountInfo);
            return false;
        }
        mountInfo->cachedFat.data = malloc (disk->sectorSz * 2);
        if (!mountInfo->cachedFat.data)
        {
            free (mbr);
            free (mountInfo);
            return false;
        }
    }
    fs->internal = mountInfo;
    return true;
}

bool FatUnmountFs (NbObject_t* fsObj)
{
    NbFileSys_t* fs = NbObjGetData (fsObj);
    FatMountInfo_t* mountInf = fs->internal;
    free (mountInf->cachedDir.data);
    free (mountInf->cachedFat.data);
    free (mountInf);
    return true;
}

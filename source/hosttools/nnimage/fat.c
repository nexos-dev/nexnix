/*
    fat.c - contains code to handle FAT filesystems
    Copyright 2022 The NexNix Project

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

/// @file fat.c

#include "fatName.h"
#include "nnimage.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libnex.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

// Standard BIOS parameter block
typedef struct _bpb
{
    uint8_t jmp[3];              // Contains 0xEB 0x?? 0x90, which jumps over BPB
    uint8_t oemName[8];          // Contains "MSWIN4.1"
    uint16_t bytesPerSector;     // Number of bytes in a sector
    uint8_t sectorPerClus;       // Number of sectors in a cluster
    uint16_t resvdSectors;       // Number of reserved sectors. nnimage sets to 4
    uint8_t fatCount;            // The number of FATs
    uint16_t rootDirEntries;     // Number of entries in root directory (only for FAT12 / FAT16)
    uint16_t sectorCount16;      // Number of sectors in volume (if non-zero)
    uint8_t media;               // 0xF8 for hard disks, 0xF0 for removable ones
    uint16_t fatSize16;          // 16 bit size of one FAT
    uint16_t sectorsPerTrack;    // Unused
    uint16_t numHeads;           // Also unused
    uint32_t hiddenSectors;      // Number of sectors preceding volume on disk
    uint32_t sectorCount32;      // Number of sectors (if non-zero)
} __attribute__ ((packed)) bpb_t;

// FAT32 boot sector
typedef struct _bootSect32
{
    bpb_t bpb;                  // Standard BPB
    uint32_t fatSize32;         // 32 bit size of FAT. Used instead of fatSize16
    uint16_t extFlags;          // Some flags that control FAT mirroring
    uint16_t fsVer;             // Version of FAT32
    uint32_t rootCluster;       // Cluster number of root directory
    uint16_t fsInfoSector;      // Sector number of FSInfo structure
    uint16_t backupBootSect;    // Backup boot sector
    uint8_t resvd[12];          // Self explanatory :)
    uint8_t driveNum;           // Drive number (unused by nnimage)
    uint8_t ntResvd;            // Reserved for Windows NT
    uint8_t sig;                // 0x29 always
    uint32_t volId;             // ID of this volume
    uint8_t label[11];          // Volume label
    uint8_t fileSys[8];         // "FAT32" is contained here
    uint8_t bootstrap[420];     // Bootstrap program
    uint16_t bootSig;           // Contains 0xAA55
} __attribute__ ((packed)) bootSector32_t;

// Boot sector structure on FAT12 and FAT16
typedef struct _bootSect
{
    bpb_t bpb;                 // Standard BPB
    uint8_t driveNum;          // Unused
    uint8_t ntResvd;           // Reserved for Windows NT's use
    uint8_t sig;               // 0x29 always
    uint32_t volId;            // Used for volume tracking
    uint8_t label[11];         // Volume label
    uint8_t fileSys[8];        // String containing "FAT12" or "FAT16" contained here. Spec says not to rely on it
    uint8_t bootstrap[448];    // Bootstrap program contained here
    uint16_t bootSig;          // Contains 0xAA55
} __attribute__ ((packed)) bootSector_t;

// Directory entry attributes
#define DIRENT_ATTR_READONLY  0x1
#define DIRENT_ATTR_HIDDEN    0x2
#define DIRENT_ATTR_SYSTEM    0x4
#define DIRENT_ATTR_VOLID     0x8
#define DIRENT_ATTR_DIRECTORY 0x10
#define DIRENT_ATTR_ARCHIVE   0x20

// Structure of a directory
typedef struct _dirEntry
{
    uint8_t shortName[11];    // Short name of file
    uint8_t attr;             // Bit 0 = read only flag, bit 1 = hidden flag, bit 3 = system flag,
                              // bit 4 = volume ID flag, bit 5 = directory flag, bit 6 = archive flag.
    uint8_t ntResvd;          // Reserved by Windows NT
    uint8_t creationMs;       // Creation time stamp component with 10ths of a second granularity
    uint16_t creationTime;    // Creation time with bits 0-4 being seconds (with 2 second granularity), bits 5-10
                              // being minute, and bits 11-16 being hour
    uint16_t creationDate;    // Creation date with bits 0-4 being day of month, bits 5-8 being
                              // month of year, and bits 9-15 being year, with epoch of Jan 1 1980
    uint16_t accessTime;      // Access time wit format mentioned above
    uint16_t clusterHigh;     // Reserved on FAT12 ang FAT16, 16 high bits of first cluster on FAT32
    uint16_t writeTime;       // Last write time
    uint16_t writeDate;       // Last write
    uint16_t cluster;         // First cluster on FAT12 and FAT16, and 16 low bits thereof FAT32
    uint32_t size;            // Size of file in bytes
} __attribute__ ((packed)) dirEntry_t;

// A directory cache entry
typedef struct _dirCache
{
    Image_t* img;           // Image of this cache entry
    dirEntry_t* dirBase;    // Base of this directory
    dirEntry_t* parent;     // Parent directory, which is the key
    uint32_t cluster;       // Cluster number to write directory to
    bool needsWrite;        // Wheter this entry needs to be flushed
} dirCacheEnt_t;

// Pointer to boot sector (FAT12 or FAT16)
static bootSector_t* bootSect = NULL;

// Pointer to boot sector that will be written to disk
static bootSector_t* endianSect = NULL;

// Macro to find the number of sectors in the root directory
#define FAT_ROOTDIRSZ(bootsect)                                                       \
    ((((bootsect)->bpb.rootDirEntries * 32) + ((bootsect)->bpb.bytesPerSector - 1)) / \
     ((bootsect)->bpb.bytesPerSector))

// Macro to find root directory base
#define FAT_ROOTDIRBASE(bootsect, fatSz) ((bootsect)->bpb.resvdSectors + ((fatSz) * (bootsect)->bpb.fatCount))

// Macro to find first data sector
#define FAT_DATASECTOR(bootsect, fatSz) \
    (((bootsect)->bpb.resvdSectors) + ((fatSz) * (bootsect)->bpb.fatCount) + rootDirSz)

// Cached root directory size
static uint32_t rootDirSz = 0;

// Cached root directory base
static uint32_t rootDirBase = 0;

// Type of FAT
static uint8_t fatType = 0;

// Pointer to FAT table. Note that this is a void pointer, as this may be 12 bits, 16 bits, or 32 bits
// Note that there are no bad sectors here, as the virtual disk doesn't have sectors underlying it
static uint8_t* fatTable = NULL;

// Pointer to root directory (only on FAT12 and FAT16)
static dirEntry_t* rootDir = NULL;

// Number of clusters in volume
static uint32_t clusterCount = 0;

// Path component last parsed
static const char* curPath = NULL;

// Partition base address
static uint64_t partBase = 0;

// List of directories that have been written
static ListHead_t* dirsList = NULL;

// Free cluster hint
static uint32_t clusterHint = 0xFFFFFFFF;

// Converts boot sector to little endian
static bool constructEndianBpb (Image_t* img)
{
    endianSect = (bootSector_t*) calloc_s (img->sectSz);
    if (!endianSect)
        return false;
    memcpy (endianSect, bootSect, sizeof (bootSector_t));
    // Chnage endian of multi byte values
    endianSect->bootSig = EndianChange16 (bootSect->bootSig, ENDIAN_LITTLE);
    endianSect->volId = EndianChange32 (bootSect->volId, ENDIAN_LITTLE);
    endianSect->bpb.bytesPerSector = EndianChange16 (bootSect->bpb.bytesPerSector, ENDIAN_LITTLE);
    endianSect->bpb.fatSize16 = EndianChange16 (bootSect->bpb.fatSize16, ENDIAN_LITTLE);
    endianSect->bpb.hiddenSectors = EndianChange32 (bootSect->bpb.hiddenSectors, ENDIAN_LITTLE);
    endianSect->bpb.numHeads = EndianChange16 (bootSect->bpb.numHeads, ENDIAN_LITTLE);
    endianSect->bpb.resvdSectors = EndianChange16 (bootSect->bpb.resvdSectors, ENDIAN_LITTLE);
    endianSect->bpb.rootDirEntries = EndianChange16 (bootSect->bpb.rootDirEntries, ENDIAN_LITTLE);
    endianSect->bpb.sectorCount16 = EndianChange16 (bootSect->bpb.sectorCount16, ENDIAN_LITTLE);
    endianSect->bpb.sectorCount32 = EndianChange32 (bootSect->bpb.sectorCount32, ENDIAN_LITTLE);
    endianSect->bpb.sectorsPerTrack = EndianChange16 (bootSect->bpb.sectorsPerTrack, ENDIAN_LITTLE);
    return true;
}

// Converts little endian boot sector to host order
static bool constructHostBootSect (Image_t* img)
{
    bootSect = (bootSector_t*) calloc_s (img->sectSz);
    if (!bootSect)
        return false;
    memcpy (bootSect, endianSect, sizeof (bootSector_t));
    // Normalize endian to host
    bootSect->bootSig = EndianChange16 (endianSect->bootSig, EndianHost());
    bootSect->volId = EndianChange32 (endianSect->volId, EndianHost());
    bootSect->bpb.bytesPerSector = EndianChange16 (endianSect->bpb.bytesPerSector, EndianHost());
    bootSect->bpb.fatSize16 = EndianChange16 (endianSect->bpb.fatSize16, EndianHost());
    bootSect->bpb.hiddenSectors = EndianChange32 (endianSect->bpb.hiddenSectors, EndianHost());
    bootSect->bpb.numHeads = EndianChange16 (endianSect->bpb.numHeads, EndianHost());
    bootSect->bpb.resvdSectors = EndianChange16 (endianSect->bpb.resvdSectors, EndianHost());
    bootSect->bpb.rootDirEntries = EndianChange16 (endianSect->bpb.rootDirEntries, EndianHost());
    bootSect->bpb.sectorCount16 = EndianChange16 (endianSect->bpb.sectorCount16, EndianHost());
    bootSect->bpb.sectorCount32 = EndianChange32 (endianSect->bpb.sectorCount32, EndianHost());
    bootSect->bpb.sectorsPerTrack = EndianChange16 (bootSect->bpb.sectorsPerTrack, EndianHost());
    return true;
}

// Initializes a FAT12 or FAT16 boot sector. Note that is doesn't initialize components that
// depend on the disk type
static bool initBootSect (Image_t* img, Partition_t* part)
{
    // Allocate it
    bootSect = calloc_s (sizeof (bootSector_t));
    if (!bootSect)
        return false;
    // Initialize basic fields
    bootSect->bootSig = 0xAA55;
    bootSect->volId = time (0);
    memcpy (bootSect->label, "NexNix Disk", 11);
    if (part->filesys == IMG_FILESYS_FAT12)
        memcpy (bootSect->fileSys, "FAT12   ", 8);
    else if (part->filesys == IMG_FILESYS_FAT16)
        memcpy (bootSect->fileSys, "FAT16   ", 8);
    else
        assert (!"initBootSect() requires a FAT12 or FAT16 partition");
    // Initialize jump to 0xEB 0x3C 0x90
    bootSect->bpb.jmp[0] = 0xEB;
    bootSect->bpb.jmp[1] = 0x3C;
    bootSect->bpb.jmp[2] = 0x90;
    // Ensure sector size is in range
    if (img->sectSz > 4096)
    {
        error ("sector size \"%d\" beyond range of 4096 for FAT volumes", img->sectSz);
        free (bootSect);
        return false;
    }
    // OEM name
    memcpy (bootSect->bpb.oemName, "MSWIN4.1", 8);
    // Sector size
    bootSect->bpb.bytesPerSector = img->sectSz;
    // Reserve 4 sectors for boot code
    bootSect->bpb.resvdSectors = 4;
    // FAT count is always 2
    bootSect->bpb.fatCount = 2;
    // Root entry count is 224 (or 112 on 720K floppies) on FAT12, 512 on FAT16
    // FAT12 is prbobably going on a floppy, hence, we want to conserve space
    if (part->filesys == IMG_FILESYS_FAT12)
        bootSect->bpb.rootDirEntries = 224;
    else if (part->filesys == IMG_FILESYS_FAT16)
        bootSect->bpb.rootDirEntries = 512;
    else
        assert (!"Invalid file system");
    // Set sector size. If greater then 0xFFFF, use sectorCount16. Else, use sectorCount32
    if (part->internal.lbaSz > 0xFFFF)
        bootSect->bpb.sectorCount32 = (uint32_t) part->internal.lbaSz;
    else if (part->internal.lbaSz < 0xFFFFFFFF)
        bootSect->bpb.sectorCount16 = (uint16_t) part->internal.lbaSz;
    else
    {
        error ("size of partition too big for FAT filesystem");
        free (bootSect);
        return false;
    }
    // Set hidden sectors
    bootSect->bpb.hiddenSectors = part->start;
    // Set BPB signature
    bootSect->sig = 0x29;
    return true;
}

// Creates the FAT table
static bool createFatTable (Image_t* img)
{
    // Allocate FAT table
    if (fatType != IMG_FILESYS_FAT32)
        fatTable = calloc_s (bootSect->bpb.fatSize16 * (uint64_t) img->sectSz);
    else
        assert (!"No FAT32 (yet)");
    if (!fatTable)
        return false;
    return true;
}

// Creates the root directory
static bool createRootDir (Image_t* img)
{
    if (fatType == IMG_FILESYS_FAT32)
        return true;    // No root directory area on FAT32
    // Allocate root directory
    rootDir = (dirEntry_t*) calloc_s (rootDirSz * (uint64_t) bootSect->bpb.bytesPerSector);
    if (!rootDir)
        return false;
    return true;
}

// Writes out a cluster value to a FAT entry
static bool writeFatEntry (uint32_t clusterIdx, uint32_t val)
{
    // Check range of cluster
    if (clusterIdx > (clusterCount + 1))
    {
        error ("cluster number out of range");
        return false;
    }
    // Figure out FAT size
    if (fatType == IMG_FILESYS_FAT32)
    {
        ;
    }
    else if (fatType == IMG_FILESYS_FAT16)
    {
        // Set entry in FAT
        uint16_t* fat = (uint16_t*) fatTable;
        fat[clusterIdx] = EndianChange16 ((uint16_t) val, ENDIAN_LITTLE);
    }
    else if (fatType == IMG_FILESYS_FAT12)
    {
        // Clear upper 4 bits of value
        val &= 0x0FFF;
        // Get index into FAT
        uint16_t fatIdx = clusterIdx + (clusterIdx / 2);
        // Grab current cluster value
        uint16_t curVal = fatTable[fatIdx] | (fatTable[fatIdx + 1] << 8);
        // If cluster index is even, then we must set low 12 bits of curVal to val and preserve the top 4 bits
        // Else, set the top 12 bits to val and preserve the bottom 4
        if (clusterIdx & 1)
        {
            // Zero out top 12 bits
            curVal &= 0x000F;
            // Shift val by 4 so it sets the top 12 bits
            val <<= 4;
        }
        else
        {
            // Zero bottom 12 bits
            curVal &= 0xF000;
        }
        // OR val into curVal
        curVal |= val;
        // Split curVal into low and high bytes so we can set the entries in the FAT table
        uint8_t low = curVal;
        uint8_t high = curVal >> 8;
        fatTable[fatIdx] = low;
        fatTable[fatIdx + 1] = high;
    }
    return true;
}

// Reads a FAT entry
static uint32_t readFatEntry (uint32_t clusterIdx)
{
    if (clusterIdx > (clusterCount + 1))
    {
        error ("cluster number out of range");
        return 0xFFFFFFFF;
    }
    if (fatType == IMG_FILESYS_FAT32)
        ;
    else if (fatType == IMG_FILESYS_FAT16)
    {
        // Return FAT entry
        uint16_t* fat = (uint16_t*) fatTable;
        return fat[clusterIdx];
    }
    else if (fatType == IMG_FILESYS_FAT12)
    {
        // Get index into FAT. We need to address 16 bit value that contains
        // all 12 bits of the cluster number
        uint16_t idx = clusterIdx + (clusterIdx / 2);
        // Get 16 bit containing all 12 bits of our wanted FAT entry so we can work on it easily
        uint16_t fatVal = fatTable[idx] | (fatTable[idx + 1] << 8);
        // If entry is even, clear top 4 bits, else, left shift by 4
        if (clusterIdx & 1)
            fatVal >>= 4;
        else
            fatVal &= 0x0FFF;
        // Return that value
        return fatVal;
    }
    return 0xFFFFFFFF;
}

// Reads a FAT cluster
static bool readCluster (Image_t* img, void* buf, uint32_t cluster)
{
    // Get start sector of this cluster
    uint32_t dataSect = 0;
    if (fatType == IMG_FILESYS_FAT32)
        ;
    else
        dataSect = FAT_DATASECTOR (bootSect, bootSect->bpb.fatSize16);
    uint64_t sector = (cluster - 2) + dataSect + partBase;
    // Get sectors per cluster
    uint8_t secPerClus = 0;
    if (fatType == IMG_FILESYS_FAT32)
        ;
    else
        secPerClus = bootSect->bpb.sectorPerClus;
    // Read in all the sectors
    for (int i = 0; i < secPerClus; ++i)
    {
        // Read in this sector
        if (!readSector (img, buf, sector + i))
            return false;
    }
    return true;
}

// Writes a FAT cluster
static bool writeCluster (Image_t* img, void* buf, uint32_t cluster)
{
    // Get start sector of this cluster
    uint32_t dataSect = 0;
    if (fatType == IMG_FILESYS_FAT32)
        ;
    else
        dataSect = FAT_DATASECTOR (bootSect, bootSect->bpb.fatSize16);
    uint64_t sector = (cluster - 2) + dataSect + partBase;
    // Get sectors per cluster
    uint8_t secPerClus = 0;
    if (fatType == IMG_FILESYS_FAT32)
        ;
    else
        secPerClus = bootSect->bpb.sectorPerClus;
    // Write out all the sectors
    for (int i = 0; i < secPerClus; ++i)
    {
        // Read in this sector
        if (!writeSector (img, buf, sector + i))
            return false;
    }
    return true;
}

// Allocates a FAT cluster
static uint32_t allocCluster (uint32_t* allocBase)
{
    // Look at hint
    if (clusterHint != 0xFFFFFFFF)
    {
        uint32_t clus = clusterHint;
        ++clusterHint;
        // Check if we have reached the top
        if (clusterHint == (clusterCount + 1))
            clusterHint = 0xFFFFFFFF;
        return clus;
    }
    else
    {
        if (!(*allocBase))
            *allocBase = 2;
        // Loop through clusters in FAT, checking if each is free
        for (uint32_t i = *allocBase; i < (clusterCount + 1); ++i)
        {
            // Is this entry free?
            if (readFatEntry (i) == 0)
            {
                // We found it!
                *allocBase = i + 1;
                return i;
            }
        }
        // No free entries. Report it
        error ("no free clusters");
        return 0xFFFFFFFF;
    }
}

bool formatFat16 (Image_t* img, Partition_t* part)
{
    return true;
}

bool formatFatFloppy (Image_t* img, Partition_t* part)
{
    // Override filesystem and sector size
    part->filesys = IMG_FILESYS_FAT12;
    img->sectSz = 512;
    // Prepare internal partition
    part->internal.lbaStart = 0;
    partBase = 0;
    if (img->sz == 720)
        part->internal.lbaSz = 1440;
    else if (img->sz == 1440)
        part->internal.lbaSz = 2880;
    else if (img->sz == 2880)
        part->internal.lbaSz = 5760;
    // Prepare basic boot sector components
    if (!initBootSect (img, part))
        return false;
    // Set geometry. This depends on the floppy type
    if (img->sz == 720)
    {
        bootSect->bpb.sectorsPerTrack = 9;
        bootSect->bpb.numHeads = 2;
        bootSect->bpb.sectorPerClus = 1;
        bootSect->bpb.rootDirEntries = 112;
        bootSect->bpb.media = 0xF8;
    }
    else if (img->sz == 1440)
    {
        bootSect->bpb.sectorsPerTrack = 18;
        bootSect->bpb.numHeads = 2;
        bootSect->bpb.sectorPerClus = 1;
        bootSect->bpb.media = 0xF0;
    }
    else if (img->sz == 2880)
    {
        bootSect->bpb.sectorsPerTrack = 36;
        bootSect->bpb.numHeads = 2;
        bootSect->bpb.sectorPerClus = 2;
        bootSect->bpb.media = 0xF0;
    }
    // Set root directory cached into
    rootDirSz = FAT_ROOTDIRSZ (bootSect);
    // Compute size of FAT using Microsoft's algorithm
    uint32_t tmp1 = part->internal.lbaSz - (bootSect->bpb.resvdSectors + rootDirSz);
    uint32_t tmp2 = (256 * bootSect->bpb.sectorPerClus) + bootSect->bpb.fatCount;
    bootSect->bpb.fatSize16 = (tmp1 + (tmp2 - 1)) / tmp2;
    // Construct endian correct BPB
    if (!constructEndianBpb (img))
    {
        free (bootSect);
        return false;
    }
    // Construct FAT table
    fatType = part->filesys;
    if (!createFatTable (img))
    {
        free (bootSect);
        free (endianSect);
        return false;
    }
    // Create root directory
    if (!createRootDir (img))
    {
        free (bootSect);
        free (endianSect);
        free (fatTable);
        return false;
    }
    // Set root directory base
    rootDirBase = FAT_ROOTDIRBASE (bootSect, bootSect->bpb.fatSize16);
    // Compute cluster count
    clusterCount = (bootSect->bpb.sectorCount16 - FAT_DATASECTOR (bootSect, bootSect->bpb.fatSize16)) /
                   bootSect->bpb.sectorPerClus;
    // Check that it is valid
    if (clusterCount > 4084)
    {
        error ("FAT12 filesystem has too many clusters");
        free (bootSect);
        free (endianSect);
        free (fatTable);
        free (rootDir);
        return false;
    }
    // Write out reserved FAT entries
    uint16_t mediaVal = 0xF00;
    mediaVal |= bootSect->bpb.media;
    writeFatEntry (0, mediaVal);
    writeFatEntry (1, 0x0FFF);
    // Initialize cluster hint
    clusterHint = 2;
    return true;
}

bool mountFat (Image_t* img, Partition_t* part)
{
    // Set up parameters on floppies
    if (img->format == IMG_FORMAT_FLOPPY)
    {
        part->filesys = IMG_FILESYS_FAT12;
        img->sectSz = 512;
        // Prepare internal partition
        part->internal.lbaStart = 0;
        if (img->sz == 720)
            part->internal.lbaSz = 1440;
        else if (img->sz == 1440)
            part->internal.lbaSz = 2880;
        else if (img->sz == 2880)
            part->internal.lbaSz = 5760;
    }
    partBase = part->internal.lbaStart;
    // Read in boot sector
    endianSect = (bootSector_t*) calloc_s (img->sectSz);
    if (!endianSect)
        return false;
    if (!readSector (img, endianSect, part->internal.lbaStart))
    {
        free (endianSect);
        return false;
    }
    // Convert to host endian
    if (!constructHostBootSect (img))
    {
        free (endianSect);
        return false;
    }

    // Set global variables
    fatType = part->filesys;
    rootDirSz = FAT_ROOTDIRSZ (bootSect);
    rootDirBase = FAT_ROOTDIRBASE (bootSect, bootSect->bpb.fatSize16);
    // Compute cluster count
    clusterCount = (bootSect->bpb.sectorCount16 - FAT_DATASECTOR (bootSect, bootSect->bpb.fatSize16)) /
                   bootSect->bpb.sectorPerClus;
    //  Set FAT type
    fatType = part->filesys;
    // Read in FAT and root directory
    fatTable = malloc_s (bootSect->bpb.fatSize16 * (size_t) bootSect->bpb.bytesPerSector);
    if (!fatTable)
    {
        free (endianSect);
        free (bootSect);
        free (fatTable);
        return false;
    }
    for (int i = 0; i < bootSect->bpb.fatSize16; ++i)
    {
        if (!readSector (img,
                         fatTable + (i * (ptrdiff_t) bootSect->bpb.bytesPerSector),
                         part->internal.lbaStart + bootSect->bpb.resvdSectors + i))
        {
            free (bootSect);
            free (fatTable);
            free (endianSect);
            return false;
        }
    }
    // Read in root directory
    rootDir = (dirEntry_t*) malloc_s (bootSect->bpb.rootDirEntries * sizeof (dirEntry_t));
    if (!rootDir)
    {
        free (bootSect);
        free (fatTable);
        free (endianSect);
        return false;
    }
    for (int i = 0; i < rootDirSz; ++i)
    {
        if (!readSector (img,
                         (void*) rootDir + (i * (ptrdiff_t) bootSect->bpb.bytesPerSector),
                         part->internal.lbaStart + rootDirBase + i))
        {
            free (bootSect);
            free (fatTable);
            free (endianSect);
            free (rootDir);
            return false;
        }
    }
    // Compute free cluster hint by starting from top
    uint32_t lastCluster = clusterCount + 1;
    uint32_t olastClus = lastCluster;
    for (uint32_t i = lastCluster; i >= 2; --i)
    {
        // Check if cluster is free. If not, we reached hint
        if (readFatEntry (i) != 0 || i == 2)
        {
            lastCluster = i + 1;
            break;
        }
    }
    // If original last cluster and found last cluster are equal, no cluster hint is ued
    if (lastCluster != olastClus)
        clusterHint = lastCluster;
    copyFileFatFloppy (img, "Makefile", "test3/test4/Make.txt");
    copyFileFatFloppy (img, "Makefile", "test3/test5/Make.txt");
    return true;
}

// Converts a file name to 8.3 form
static bool toDosName (char* name, char* buf)
{
    size_t nameLen = strlen (name);
    char* obuf = buf;
    // Convert name from UTF8 to UTF32
    char32_t* _name = (char32_t*) calloc_s ((nameLen + 1) * sizeof (char32_t));
    if (!_name)
        return false;
    char32_t* oname = _name;
    while (*name)
    {
        // Go to UTF-32
        name += UnicodeDecode8 (_name, (const uint8_t*) name, nameLen);
        ++_name;
    }
    _name = oname;
    // Ensure first character isn't extension marker
    if (*_name == '.')
        *_name = '_';
    // Set buf to spaces
    memset (buf, ' ', 11);
    // Copy up to 8 characters from name to buf, converting to upper case
    int i = 0;
    while (*_name && *_name != '.' && i < 8)
    {
        char32_t c = *_name;
        // Validate c
        if (c < 0x20 || c > 0x80 || !fatValidChars[c])
            c = 95;    // Underscore
        // Add to buf
        buf[i] = (char) toupper ((char) c);
        ++_name;
        ++i;
    }
    // Set extension
    buf = obuf + 8;
    // Advance to last period in _name.
    char32_t* perPtr = c32rchr (oname, '.');
    if (!perPtr)
    {
        free (oname);
        return true;    // If there is no extension, we are done
    }
    ++perPtr;    // Go passed period
    // Now copy
    i = 0;
    while (*perPtr && i < 3)
    {
        char32_t c = *perPtr;
        // Validate c
        if ((c == 0x20 && i == 0) || c < 0x20 || c > 0xFF || !fatValidChars[c])
            c = 95;    // Underscore
        // Add to buf
        buf[i] = (char) toupper ((char) c);
        ++perPtr;
        ++i;
    }
    free (oname);
    return true;
}

// Parses a component of a path name, and converts it to 8.3
static bool parsePath (char* buf)
{
    char comp[256];
    int i = 0;
    // Skip over any current '/'
    if (*curPath == '/')
        ++curPath;
    // Go to next seperator
    while (*curPath != '/' && *curPath)
    {
        comp[i] = *curPath;
        ++i;
        ++curPath;
    }
    comp[i] = 0;
    // Convert to 8.3
    if (!toDosName (comp, buf))
        return false;
    return true;
}

// Checks if current path component is the last one
static inline bool isLastPathComp()
{
    return *curPath == 0;
}

// Find by function for dirEntry_t cache
static bool dirsFindBy (ListEntry_t* entry, void* data)
{
    dirCacheEnt_t* cacheEnt = ListEntryData (entry);
    return cacheEnt->parent == (dirEntry_t*) data;
}

// Destroys a directory cache entry
static void dirsDestroyEnt (void* data)
{
    dirCacheEnt_t* cacheEnt = data;
    // Write out this entry if needed
    if (cacheEnt->needsWrite)
        writeCluster (cacheEnt->img, (void*) cacheEnt->dirBase, cacheEnt->cluster);
    free (cacheEnt->dirBase);
    free (data);
}

// EOF values for different FAT types
static uint32_t fatEofStart[] = {0, 0x0FFFFFF8, 0xFFF8, 0x0FF8, 0, 0};

// Reads in a subdirectory of another directory
static dirEntry_t* readSubDir (Image_t* img, dirEntry_t* parent, size_t* numEnt)
{
    uint8_t secPerClus = 0;
    if (fatType == IMG_FILESYS_FAT32)
        secPerClus = 1;
    else
        secPerClus = bootSect->bpb.sectorPerClus;
    *numEnt = (img->sectSz * (size_t) secPerClus) / sizeof (dirEntry_t);
    // Allocate directory list if needed
    if (!dirsList)
    {
        dirsList = ListCreate ("dirEntry_t", false, 0);
        ListSetFindBy (dirsList, dirsFindBy);
        ListSetDestroy (dirsList, dirsDestroyEnt);
    }
    // Find the directory
    ListEntry_t* ent = ListFindEntryBy (dirsList, parent);
    if (ent)
    {
        // We found it! Return this base
        return ((dirCacheEnt_t*) ListEntryData (ent))->dirBase;
    }
    // Grab cluster of directory
    uint32_t cluster = parent->cluster;
    if (fatType == IMG_FILESYS_FAT32)
        cluster |= (parent->clusterHigh << 16);
    // Allocate space for directory
    dirEntry_t* dir = calloc_s (*numEnt * sizeof (dirEntry_t));
    // Read in current cluster
    if (!readCluster (img, dir, cluster))
    {
        free (dir);
        return NULL;
    }
    // Ensure EOF is next
    uint32_t eofEnt = readFatEntry (cluster);
    if (eofEnt == 0xFFFFFFFF || !(eofEnt >= fatEofStart[fatType]))
    {
        free (dir);
        return NULL;
    }
    // Cache it
    dirCacheEnt_t* cacheEnt = calloc_s (sizeof (dirCacheEnt_t));
    cacheEnt->dirBase = dir;
    cacheEnt->parent = parent;
    cacheEnt->img = img;
    cacheEnt->cluster = cluster;
    ListAddFront (dirsList, cacheEnt, 0);
    return dir;
}

// Finds a file
static dirEntry_t* findFile (Image_t* img, const char* path, bool* isError)
{
    // Path stuff
    curPath = path;
    char dosName[12];
    // The current directory being operated on
    dirEntry_t* curDir = rootDir;
    size_t numEntries = (rootDirSz * (size_t) img->sectSz) / sizeof (dirEntry_t);
    while (1)
    {
        // Get current path component
        if (!parsePath (dosName))
        {
            *isError = true;
            return NULL;
        }
        // Parse directory contents, looking for dosName
        bool isFound = false;
        for (int i = 0; i < numEntries; ++i)
        {
            // Check if this entry is free
            if (curDir[i].shortName[0] == 0xE5)
                continue;    // Go to next entry
            else if (curDir[i].shortName[0] == 0x00)
                break;    // Wasn't found
            if (curDir[i].attr & DIRENT_ATTR_VOLID)
                continue;
            // Check if we found the file
            if (!memcmp (curDir[i].shortName, dosName, 11))
            {
                // We found it!
                curDir = &curDir[i];
                isFound = true;
                break;
            }
        }
        // Check if we found it
        if (isFound)
        {
            // If this is the last component of the path, then
            // curDir must point to a file. Else, it must point to a directory
            if ((isLastPathComp() && (curDir->attr & DIRENT_ATTR_DIRECTORY) == DIRENT_ATTR_DIRECTORY) ||
                (!isLastPathComp() && (curDir->attr & DIRENT_ATTR_DIRECTORY) != DIRENT_ATTR_DIRECTORY))
            {
                // We expected a file, but got a directory (or the inverse). Not an error, but
                // it indicates that the file doesn't exist
                *isError = false;
                return NULL;
            }
            // Read in next directory if needed
            if (!isLastPathComp())
            {
                curDir = readSubDir (img, curDir, &numEntries);
                if (!curDir)
                {
                    *isError = false;
                    return NULL;
                }
            }
            else
            {
                // We are finished
                *isError = false;
                return curDir;
            }
        }
        else
        {
            // File doesn't exist
            *isError = false;
            return NULL;
        }
    }
    *isError = false;
    return NULL;
}

// Create a DOS date
static void createDosDate (uint16_t* date, uint16_t* tm, uint16_t* ms)
{
    // Get Unix UTC time
    time_t unixTime = time (NULL);
    struct tm* dateTime = gmtime (&unixTime);
    // Set up date
    *date = 0;
    *date |= (dateTime->tm_year - 80) << 9;
    *date |= (dateTime->tm_mon + 1) << 5;
    *date |= dateTime->tm_mday;
    // Set up time
    *tm |= dateTime->tm_hour << 11;
    *tm |= dateTime->tm_min << 5;
    *tm |= dateTime->tm_sec / 2;
    // Set milliseconds. We set it to 0 for now
    *ms = 0;
}

// Creates a directory entry
static dirEntry_t* createFatDirs (Image_t* img, const char* path)
{
    // Path variables
    char dosName[12];
    curPath = path;
    // Set up current directory
    dirEntry_t* curDir = rootDir;
    dirEntry_t* parent = NULL;
    size_t numDirEntries = (rootDirSz * (size_t) img->sectSz) / sizeof (dirEntry_t);
    // Start looping through paths
    while (1)
    {
        if (!parsePath (dosName))
            return NULL;

        if (isLastPathComp())
            return curDir;

        // Attempt to find this directory entry.
        // Note that we also look for the first free entry in this search
        dirEntry_t* firstFreeEnt = curDir;
        bool foundEnt = false;
        bool foundFreeEnt = false;
        for (int i = 0; i < numDirEntries; ++i)
        {
            // Check if this entry is free
            if (curDir[i].shortName[0] == 0xE5 || curDir[i].shortName[0] == 0x05)
            {
                foundFreeEnt = true;
                firstFreeEnt = &curDir[i];
            }
            // If entry is zeroed, then go ahead and stop searching
            if (curDir[i].shortName[0] == 0)
            {
                firstFreeEnt = &curDir[i];
                foundFreeEnt = true;
                break;
            }
            // Check if this entry is pertinent
            if (curDir[i].attr & DIRENT_ATTR_VOLID)
                continue;
            // Check if it matches
            if (!memcmp (curDir[i].shortName, dosName, 11))
            {
                // We found the entry. There is no need to create a new directory entry
                foundEnt = true;
                curDir = &curDir[i];
                break;
            }
        }
        // Check if the entry was found
        if (foundEnt)
        {
            // Descend into next subdirectory
            parent = curDir;
            curDir = readSubDir (img, curDir, &numDirEntries);
            if (!curDir)
                return NULL;
        }
        else if (foundFreeEnt)
        {
            // Invalidate directory
            if (parent)
            {
                ListEntry_t* ent = ListFindEntryBy (dirsList, parent);
                if (ent)
                {
                    dirCacheEnt_t* dirEnt = ListEntryData (ent);
                    dirEnt->needsWrite = true;
                }
            }
            // Create new directory
            memset (firstFreeEnt, 0, sizeof (dirEntry_t));
            memcpy (firstFreeEnt->shortName, dosName, 11);
            firstFreeEnt->attr |= DIRENT_ATTR_DIRECTORY;
            // Set dates and times
            uint16_t date = 0, time = 0, ms = 0;
            createDosDate (&date, &time, &ms);
            firstFreeEnt->accessTime = time;
            firstFreeEnt->writeDate = date;
            firstFreeEnt->writeTime = time;
            firstFreeEnt->creationDate = date;
            firstFreeEnt->creationTime = time;
            firstFreeEnt->creationMs = ms;
            // Compute size of directory
            uint8_t secPerClus = 0;
            if (fatType == IMG_FILESYS_FAT32)
                ;
            else
                secPerClus = bootSect->bpb.sectorPerClus;
            numDirEntries = (img->sectSz * (size_t) secPerClus) / sizeof (dirEntry_t);
            // Allocate directory table
            dirEntry_t* dirBase = (dirEntry_t*) calloc_s (numDirEntries * sizeof (dirEntry_t));
            if (!dirBase)
                return NULL;
            // Add . and .. to new directory
            memcpy (dirBase[0].shortName, ".          ", 11);
            dirBase[0].attr = DIRENT_ATTR_DIRECTORY;
            dirBase[0].size = 0;
            dirBase[0].accessTime = time;
            dirBase[0].creationDate = date;
            dirBase[0].creationTime = time;
            dirBase[0].creationMs = ms;
            dirBase[0].writeDate = date;
            dirBase[0].writeTime = time;
            // Add ..
            memcpy (dirBase[1].shortName, "..         ", 11);
            dirBase[1].attr = DIRENT_ATTR_DIRECTORY;
            dirBase[1].size = 0;
            dirBase[1].accessTime = time;
            dirBase[1].creationDate = date;
            dirBase[1].creationTime = time;
            dirBase[1].creationMs = ms;
            dirBase[1].writeDate = date;
            dirBase[1].writeTime = time;
            if (parent)
            {
                dirBase[1].cluster = parent->cluster;
                dirBase[1].clusterHigh = parent->clusterHigh;
            }
            // Cache this directory entry
            // Allocate directory list if needed
            if (!dirsList)
            {
                dirsList = ListCreate ("dirEntry_t", false, 0);
                ListSetFindBy (dirsList, dirsFindBy);
                ListSetDestroy (dirsList, dirsDestroyEnt);
            }
            dirCacheEnt_t* cacheEnt = malloc_s (sizeof (dirCacheEnt_t));
            if (!cacheEnt)
                return NULL;
            cacheEnt->dirBase = dirBase;
            cacheEnt->parent = firstFreeEnt;
            cacheEnt->img = img;
            cacheEnt->needsWrite = true;
            ListEntry_t* lent = ListAddFront (dirsList, cacheEnt, 0);
            if (!lent)
            {
                free (dirBase);
                free (cacheEnt);
                return NULL;
            }
            // Allocate cluster
            uint32_t clusterBase = 0;
            cacheEnt->cluster = allocCluster (&clusterBase);
            if (cacheEnt->cluster == 0xFFFFFFFF)
            {
                // No good. Disk is full
                ListRemove (dirsList, lent);
                free (dirBase);
                free (cacheEnt);
                return NULL;
            }
            // Set initial cluster
            firstFreeEnt->cluster = cacheEnt->cluster & 0xFFFF;
            if (fatType == IMG_FILESYS_FAT32)
                firstFreeEnt->clusterHigh = cacheEnt->cluster >> 16;
            // Set .'s cluster
            dirBase[0].cluster = firstFreeEnt->cluster;
            dirBase[0].clusterHigh = firstFreeEnt->clusterHigh;
            // Write EOF to FAT
            writeFatEntry (cacheEnt->cluster, fatEofStart[fatType]);
            curDir = dirBase;
            parent = firstFreeEnt;
        }
        else
        {
            // Report error
            error ("%s: No free directory entries", dosName);
            return NULL;
        }
    }
    return NULL;
}

// Overwrites FAT chain in a file
static bool overwriteFileFat (Image_t* img, dirEntry_t* entry)
{
    // Check if file has any data
    if (!entry->size)
        return true;    // Nothing to do
    // Zero out size of entry
    entry->size = 0;
    // Grab first cluster of file
    uint32_t initClusterVal = entry->cluster;
    if (fatType == IMG_FILESYS_FAT32)
        initClusterVal |= (entry->clusterHigh << 16);
    // Grab next cluster so we can mark initial as EOF
    uint32_t nextCluster = readFatEntry (initClusterVal);
    writeFatEntry (initClusterVal, fatEofStart[fatType]);
    //  Iterate through cluster chain
    while (!(nextCluster >= fatEofStart[fatType]))
    {
        // Grab next cluster
        uint32_t oldCluster = nextCluster;
        nextCluster = readFatEntry (nextCluster);
        writeFatEntry (oldCluster, 0);
    }
    entry->cluster = 0;
    entry->clusterHigh = 0;
    return true;
}

// Copies a file to a FAT floppy (FAT12)
bool copyFileFatFloppy (Image_t* img, const char* src, const char* dest)
{
    // Find dest in the disk
    bool isError = false;
    dirEntry_t* fileEnt = findFile (img, dest, &isError);
    if (isError)
        return false;
    // So, if the file exists, we must check if it needs to be updated.
    // Let's do that next
    if (fileEnt)
    {
        // Convert last modification time stamp of file entry to Unix time
        int32_t year = (fileEnt->writeDate >> 9) + 1980;
        int32_t month = BitClearRangeNew ((fileEnt->writeDate >> 5), 4, 12);
        int32_t day = BitClearRangeNew (fileEnt->writeDate, 5, 11);
        int32_t hour = fileEnt->writeTime >> 11;
        int32_t minute = BitClearRangeNew ((fileEnt->writeTime >> 5), 6, 10);
        int32_t second = BitClearRangeNew (fileEnt->writeTime, 5, 11) * 2;
        // Convert to Unix time
        struct tm time = {0};
        time.tm_year = year - 1900;
        time.tm_mon = month - 1;
        time.tm_mday = day;
        time.tm_hour = hour;
        time.tm_min = minute;
        time.tm_sec = second;
        time_t destTime = timegm (&time);
        // Stat source to get modification timestamp
        struct stat st;
        if (stat (src, &st) == -1)
        {
            error ("%s:%s", src, strerror (errno));
            return false;
        }
        time_t srcTime = st.st_mtim.tv_sec;
        if (srcTime > destTime)
        {
            // Overwrite file
            overwriteFileFat (img, fileEnt);
        }
        else
        {
            // Nothing to do, file is up to date
            return true;
        }
    }
    else
    {
        // Create directory entry
        fileEnt = createFatDirs (img, dest);
        if (!fileEnt)
            return false;
    }
    return true;
}

bool cleanupFat (Image_t* img, Partition_t* part)
{
    // Cleanup directory list
    if (dirsList)
        ListDestroy (dirsList);
    // Write out the BPB
    if (!writeSector (img, endianSect, part->internal.lbaStart))
    {
        free (bootSect);
        if (part->filesys != IMG_FILESYS_FAT32)
            free (rootDir);
        free (fatTable);
        free (endianSect);
        return false;
    }
    // Write out FAT
    uint32_t fatStart = bootSect->bpb.resvdSectors;
    for (int i = 0; i < bootSect->bpb.fatCount; ++i)
    {
        if (part->filesys != IMG_FILESYS_FAT32)
        {
            for (int i = 0; i < bootSect->bpb.fatSize16; ++i)
            {
                if (!writeSector (img,
                                  fatTable + (i * (uint64_t) bootSect->bpb.bytesPerSector),
                                  part->internal.lbaStart + fatStart + i))
                {
                    free (bootSect);
                    if (part->filesys != IMG_FILESYS_FAT32)
                        free (rootDir);
                    free (fatTable);
                    free (endianSect);
                    return false;
                }
            }
            fatStart += bootSect->bpb.fatSize16;
        }
    }
    // Write out root directory
    if (part->filesys != IMG_FILESYS_FAT32)
    {
        for (int i = 0; i < rootDirSz; ++i)
        {
            uint8_t* _rootDir = (uint8_t*) rootDir;
            if (!writeSector (img,
                              (void*) (_rootDir + (i * (uint64_t) bootSect->bpb.bytesPerSector)),
                              part->internal.lbaStart + rootDirBase + i))
            {
                free (bootSect);
                if (part->filesys != IMG_FILESYS_FAT32)
                    free (rootDir);
                free (fatTable);
                free (endianSect);
                return false;
            }
        }
    }
    free (bootSect);
    if (part->filesys != IMG_FILESYS_FAT32)
        free (rootDir);
    free (fatTable);
    free (endianSect);
    return true;
}

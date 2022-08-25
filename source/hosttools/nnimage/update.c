/*
    update.c - contains functions to update disk images
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

/// @file update.c

// WARNING: If you value your sanity, please, close this file and move on
// This code is a mess, it is need of a large overhaul
// It also is in need of optimization

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <guestfs.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "nnimage.h"
#include <libnex.h>

// File descriptor of list file
static FILE* listFileFd = 0;

// File name
static const char* listFileName = NULL;

// Mount directory
static const char* mountDir = NULL;

// Prefix on host
static const char* hostPrefix = NULL;

// Partition of update state
static Partition_t* curPart = NULL;

// Converts multiplier value to sectors
#define IMG_MUL_TO_SECT(mulSz) (((mulSz) * (muls[img->mul])) / 512)

// File entry in list file
typedef struct _lstEntry
{
    const char* destFile;    // File to copy to
    const char* srcFile;     // File to copy from
} listFile_t;

// Gets next file entry
static listFile_t* getNextFile()
{
    // Begin reading in string
    static char buf[255];
    static char dest[255];
    static char src[255];
readLoop:
    memset (buf, 0, 255);
    if (!fgets (buf, 255, listFileFd) && ferror (listFileFd))
    {
        error ("%s: %s", listFileName, strerror (errno));
        return (listFile_t*) -1;
    }
    else if (feof (listFileFd))
        return NULL;
    listFile_t* fileEnt = (listFile_t*) calloc_s (sizeof (listFile_t));
    // Attempt to find line end
    bool lineEndFound = false;
    for (int i = 0; i < 255; ++i)
    {
        // CHeck if this is line ending
        if (buf[i] == '\n')
        {
            lineEndFound = true;
            buf[i] = 0;
            // If previous character was carriage return, erase that as well
            if (buf[i - 1] == '\r')
                buf[i - 1] = 0;
            break;
        }
    }
    if (!lineEndFound)
    {
        // That's an error
        error ("no line end detected");
        return (listFile_t*) -1;
    }
    // Check if this pertains to this partition
    char* prefix = curPart->prefix;
    // Check is this is root. If the string contains only a slash, it is root
    if (curPart->prefix[1] == 0)
    {
        ++prefix;
        // If buf starts without a slash and the prefix is not root
        // then we skip over this file
        if (*buf == '/')
            goto readLoop;    // Go to next file
    }
    else
    {
        // Make sure first part of string is prefix
        if (strstr (buf, prefix) != buf)
            goto readLoop;
    }
    // Strip that part
    size_t prefixLen = strlen (prefix);
    char* file = buf + prefixLen;
    // Prepare source buffer
    if (strlcpy (src, hostPrefix, 255) >= 254)
    {
        error ("prefix too large");
        return NULL;
    }
    // Detect if a '/' is at the end
    if (src[strlen (hostPrefix) - 1] != '/' && buf[0] != '/')
    {
        src[strlen (hostPrefix)] = '/';
        src[strlen (hostPrefix) + 1] = '\0';
    }
    // Concatenate source file to this
    if (strlcat (src, buf, 255) >= 255)
    {
        error ("prefix too large");
        return NULL;
    }
    // Prepare dest
    if (strlcpy (dest, mountDir, 255) >= 254)
    {
        error ("prefix too large");
        return NULL;
    }
    // Detect if a '/' is at the end
    if (dest[strlen (mountDir) - 1] != '/')
    {
        dest[strlen (mountDir)] = '/';
        dest[strlen (mountDir) + 1] = '\0';
    }
    // Concatenate source file to this
    if (strlcat (dest, file, 255) >= 255)
    {
        error ("prefix too large");
        return NULL;
    }
    // Store file buffer
    fileEnt->destFile = dest;
    fileEnt->srcFile = src;
    return fileEnt;
}

#define BLKSIZET (1024 * (size_t) 1024)
#define BLKSIZE  (1024 * (off_t) 1024)

// Copies a file out
static bool copyFile (guestfs_h* guestFs,
                      const char* src,
                      const char* dest,
                      off_t srcSz)
{
    uint8_t* buf = (uint8_t*) malloc_s (BLKSIZET);
    // Get number of 1M blocks, rounding up
    off_t blocks = (srcSz + (BLKSIZE - 1)) / (BLKSIZE);
    // Open src
    int srcFd = open (src, O_RDONLY);
    if (srcFd == -1)
    {
        free (buf);
        printf ("%s: %s\n", src, strerror (errno));
        return false;
    }
    for (int i = 0; i < blocks; ++i)
    {
        // Read in data from src
        ssize_t bytesRead = read (srcFd, buf, BLKSIZE);
        if (bytesRead == -1)
        {
            free (buf);
            printf ("%s: %s\n", src, strerror (errno));
            close (srcFd);
            return false;
        }
        // Write it out
        if (guestfs_write_append (guestFs,
                                  dest,
                                  (const char*) buf,
                                  (size_t) bytesRead) == -1)
        {
            free (buf);
            close (srcFd);
            return false;
        }
    }
    free (buf);
    close (srcFd);
    return true;
}

bool updateFile (guestfs_h* guestFs, const char* src, const char* dest);

// Updates a symlink
static bool updateSymlink (guestfs_h* guestFs,
                           const char* src,
                           const char* dest,
                           struct stat* srcSt)
{
    // Allocate buffer for readlink
    char* linkName = malloc_s (srcSt->st_size + 1);
    char* olinkName = linkName;
    if (!linkName)
        return false;
    // Read in symlink
    if (readlink (src, linkName, srcSt->st_size + 1) < 0)
    {
        free (linkName);
        return false;
    }
    linkName[srcSt->st_size] = 0;
    // Strip absoulte part of prefix
    size_t prefixLen = strlen (hostPrefix);
    if (linkName[0] == '/')
        linkName += prefixLen;
    // Prepend destdir
    char* fullLink = malloc_s (prefixLen + srcSt->st_size + 2);
    if (!fullLink)
    {
        free (olinkName);
        return false;
    }
    strcpy (fullLink, hostPrefix);
    // Maybe add a trailing '/'
    if (fullLink[prefixLen - 1] != '/')
    {
        fullLink[prefixLen] = '/';
        fullLink[prefixLen + 1] = 0;
    }
    strcat (fullLink, linkName);
    // Prepend mountdir now
    size_t mountLen = strlen (mountDir);
    char* fullDest = malloc_s (prefixLen + mountLen + 2);
    if (!fullDest)
    {
        free (olinkName);
        free (fullLink);
        return false;
    }
    strcpy (fullDest, mountDir);
    // Make sure we skip over prefix if not on root partition
    if (!strcmp (curPart->prefix, "/"))
    {
        // Maybe add a trailing '/'
        if (fullDest[mountLen - 1] != '/')
        {
            fullDest[mountLen] = '/';
            fullDest[mountLen + 1] = 0;
        }
        strcat (fullDest, linkName);
    }
    else
    {
        // Drop a trailing '/'
        if (fullDest[mountLen - 1] == '/')
            fullDest[mountLen - 1] = 0;
        // Concatenate, skipping partition prefix
        strcat (fullDest, linkName + strlen (curPart->prefix));
    }
    // Ensure link target exists
    char* backupFullDest = strdup (fullDest);
    if (!updateFile (guestFs, fullLink, fullDest))
    {
        free (olinkName);
        free (fullDest);
        free (fullLink);
        free (backupFullDest);
        return false;
    }
    char* backupDest = strdup (dest);
    if (guestfs_mkdir_p (guestFs, dirname ((char*) dest)) == -1)
    {
        free (backupDest);
        free (olinkName);
        free (fullDest);
        free (fullLink);
        free (backupFullDest);
        return false;
    }
    // Create symlink
    if (guestfs_ln_sf (guestFs, linkName, backupDest) == -1)
    {
        free (backupDest);
        free (olinkName);
        free (fullDest);
        free (fullLink);
        free (backupFullDest);
        return false;
    }
    free (olinkName);
    free (fullDest);
    free (fullLink);
    free (backupDest);
    free (backupFullDest);
    return true;
}

// Updates a regular file
static bool updateRegFile (guestfs_h* guestFs,
                           const char* src,
                           const char* dest,
                           struct stat* srcSt)
{
    // Check for dest, and maybe stat it
    struct guestfs_statns* destSt = NULL;
    bool destExist = false;
    int doesExist = guestfs_is_file (guestFs, dest);
    if (doesExist == -1)
        return false;
    else if (doesExist == 1)
    {
        destSt = guestfs_statns (guestFs, dest);
        if (destSt == NULL)
            return false;
        destExist = true;
    }

    // Check if we need to update this file
    if (!destExist || (srcSt->st_mtim.tv_sec > destSt->st_mtime_sec))
    {
        // Create directories
        // Backup dest
        char* backupDest = strdup (dest);
        if (!backupDest)
        {
            if (destSt)
                guestfs_free_statns (destSt);
            return false;
        }
        if (guestfs_mkdir_p (guestFs, dirname ((char*) dest)) == -1)
        {
            if (destSt)
                guestfs_free_statns (destSt);
            free (backupDest);
            return false;
        }
        // Create file if it doesn't exist, else truncate it
        if (!destExist)
        {
            if (guestfs_touch (guestFs, backupDest) == -1)
            {
                if (destSt)
                    guestfs_free_statns (destSt);
                free (backupDest);
                return false;
            }
        }
        else
        {
            if (guestfs_truncate (guestFs, backupDest) == -1)
            {
                if (destSt)
                    guestfs_free_statns (destSt);
                free (backupDest);
                return false;
            }
        }
        // Copy out actual file
        if (!copyFile (guestFs, src, backupDest, srcSt->st_size))
        {
            if (destSt)
                guestfs_free_statns (destSt);
            free (backupDest);
            return false;
        }
        // Cleanup
        if (destSt)
            guestfs_free_statns (destSt);
        free (backupDest);
        return true;
    }
    else
    {
        if (destSt)
            guestfs_free_statns (destSt);
        return true;
    }
}

// Copies out a whole subdirectory
bool updateSubDir (guestfs_h* guestFs, const char* srcDir, const char* destDir)
{
    DIR* src = opendir (srcDir);
    if (!src)
    {
        error ("%s: %s", srcDir, strerror (errno));
        return false;
    }
    // Loop through directory
    struct dirent* curDir = readdir (src);
    while (curDir != NULL)
    {
        // Check if this is . or ..
        if (!strcmp (curDir->d_name, ".") || !strcmp (curDir->d_name, ".."))
            goto nextDirEnt;
        // Create destination variable
        char* fullDest = malloc_s (255);
        if (!fullDest)
        {
            closedir (src);
            return NULL;
        }
        if (strlcpy (fullDest, destDir, 255) >= 255)
        {
            error ("buffer overflow detected");
            return false;
        }
        // Check if we need to add a trailing '/' as a seperator
        size_t destLen = strlen (destDir);
        if (fullDest[destLen - 1] != '/')
        {
            fullDest[destLen] = '/';
            fullDest[destLen + 1] = 0;
        }
        if (strlcat (fullDest, curDir->d_name, 255) >= 255)
        {
            error ("buffer overflow detected");
            return false;
        }
        // Prepend prefix to source
        char* fullSrc = malloc_s (255);
        if (!fullSrc)
        {
            closedir (src);
            free (fullDest);
            return false;
        }
        if (strlcpy (fullSrc, srcDir, 255) >= 255)
        {
            error ("buffer overflow detected");
            return false;
        }
        // Check if we need to add a trailing '/' as a seperator
        size_t srcLen = strlen (srcDir);
        if (fullSrc[srcLen - 1] != '/')
        {
            fullSrc[srcLen] = '/';
            fullSrc[srcLen + 1] = 0;
        }
        if (strlcat (fullSrc, curDir->d_name, 255) >= 255)
        {
            error ("buffer overflow detected");
            return false;
        }
        if (!updateFile (guestFs, fullSrc, fullDest))
        {
            closedir (src);
            free (fullDest);
            free (fullSrc);
            return false;
        }
        free (fullDest);
        free (fullSrc);
    nextDirEnt:
        curDir = readdir (src);
    }
    closedir (src);
    return true;
}

// Updates a file on disk
bool updateFile (guestfs_h* guestFs, const char* src, const char* dest)
{
    // Stat source
    struct stat srcSt;
    if (lstat (src, &srcSt) == -1)
    {
        error ("%s: %s", src, strerror (errno));
        return false;
    }
    if (S_ISLNK (srcSt.st_mode))
        return updateSymlink (guestFs, src, dest, &srcSt);
    else if (S_ISREG (srcSt.st_mode))
        return updateRegFile (guestFs, src, dest, &srcSt);
    else if (S_ISDIR (srcSt.st_mode))
        return updateSubDir (guestFs, src, dest);
    else
    {
        error ("%s is not regular file, symlink, or directory");
        return false;
    }
}

bool updatePartition (Image_t* img,
                      Partition_t* part,
                      const char* listFile,
                      const char* mount,
                      const char* host)
{
    printf ("Updating partition %s on prefix %s...\n", part->name, part->prefix);
    mountDir = mount;
    hostPrefix = host;
    curPart = part;
    listFileName = listFile;
    // Open list file
    listFileFd = fopen (listFile, "r");
    if (!listFileFd)
    {
        error ("%s: %s", listFile, strerror (errno));
        return false;
    }
    FILE* xorrisoList = NULL;
    if (part->filesys == IMG_FILESYS_ISO9660)
    {
        // On ISO 9660 partitions, create xorriso list file
        const char* xorrisoListFile = "xorrisolst.txt";
        xorrisoList = fopen (xorrisoListFile, "w+");
        if (!xorrisoList)
        {
            error ("%s: %s", xorrisoListFile, strerror (errno));
            fclose (listFileFd);
            return false;
        }
    }
    listFile_t* curFile = getNextFile();
    while (curFile)
    {
        if (curFile == (listFile_t*) -1)
            return false;
        if (part->filesys != IMG_FILESYS_ISO9660)
        {
            // Update file
            if (!updateFile (img->guestFs, curFile->srcFile, curFile->destFile))
            {
                free (curFile);
                fclose (listFileFd);
                return false;
            }
        }
        else
        {
            // Ensure this partition is root
            if (curPart->prefix[1] != 0)
            {
                error ("ISO9660 partition must be root");
                free (curFile);
                (void) fclose (listFileFd);
                (void) fclose (xorrisoList);
                return false;
            }
            // Append path
            fprintf (xorrisoList,
                     "%s=%s\n",
                     curFile->destFile + strlen (mountDir) + 1,
                     curFile->srcFile);
        }
        free (curFile);
        curFile = getNextFile();
    }
    if (xorrisoList)
    {
        // Ensure boot image is in disk image
        if (getBootPart (img))
            fprintf (xorrisoList,
                     "%s=%s\n",
                     basename (strdup (getenv ("NNBOOTIMG"))),
                     getenv ("NNBOOTIMG"));
        if (getAltBootPart (img))
            fprintf (xorrisoList,
                     "%s=%s\n",
                     basename (strdup (getenv ("NNALTBOOTIMG"))),
                     getenv ("NNALTBOOTIMG"));
        fclose (xorrisoList);
    }
    fclose (listFileFd);
    return true;
}

// Updates the VBR of a partition
bool updateVbr (Image_t* img, Partition_t* part)
{
    // Get start of VBR. If this is an ISO9660 boot image, skip this
    loff_t vbrBase = 0;
    const char* file = NULL;
    if (img->format == IMG_FORMAT_ISO9660)
    {
        file = getenv ("NNBOOTIMG");
        if (img->bootEmu == IMG_BOOTEMU_HDD)
            vbrBase = IMG_MUL_TO_SECT (part->start) * (loff_t) IMG_SECT_SZ;
    }
    else if (img->format == IMG_FORMAT_FLOPPY)
        file = img->file;
    else
    {
        file = img->file;
        vbrBase = IMG_MUL_TO_SECT (part->start) * (loff_t) IMG_SECT_SZ;
    }
    assert (file);
    // Open up image
    int imgFd = open (file, O_RDWR);
    if (imgFd == -1)
    {
        error ("%s:%s", file, strerror (errno));
        return false;
    }
    // Read in VBR
    uint8_t vbrBuf[IMG_SECT_SZ * 2];
    if (pread (imgFd, vbrBuf, IMG_SECT_SZ, vbrBase) == -1)
    {
        error ("%s: %s", file, strerror (errno));
        close (imgFd);
        return false;
    }
    // Get size of VBR
    struct stat st;
    if (stat (part->vbrFile, &st) == -1)
    {
        error ("%s: %s", part->vbrFile, strerror (errno));
        close (imgFd);
        return false;
    }
    if (st.st_size > (long) (IMG_SECT_SZ * 2))
    {
        error ("%s: maximum size of VBR is %d bytes", part->vbrFile, IMG_SECT_SZ);
        close (imgFd);
        return false;
    }
    // Open up VBR file
    int vbrFd = open (part->vbrFile, O_RDONLY);
    if (vbrFd == -1)
    {
        error ("%s:%s", part->vbrFile, strerror (errno));
        close (imgFd);
        return false;
    }
    // Get size of BPB
    short bpbSize = 0;
    if (part->filesys == IMG_FILESYS_FAT12 || part->filesys == IMG_FILESYS_FAT16)
        bpbSize = 62;
    else if (part->filesys == IMG_FILESYS_FAT32)
        bpbSize = 90;
    else
    {
        // Not valid
        error ("VBR must be installed on FAT12, FAT16, or FAT32 partition");
        close (imgFd);
        close (vbrFd);
        return false;
    }
    // Read into VBR buffer
    if (pread (vbrFd, vbrBuf + bpbSize, st.st_size - bpbSize, bpbSize) == -1)
    {
        error ("%s:%s", part->vbrFile, strerror (errno));
        close (imgFd);
        close (vbrFd);
        return false;
    }
    if (img->format != IMG_FORMAT_FLOPPY && img->bootEmu != IMG_BOOTEMU_FDD)
    {
        // Patch JMP instruction at start of VBR to skip partitoin base
        vbrBuf[0] = 0xEB;
        vbrBuf[1] = 0x5E;
        vbrBuf[2] = 0x90;
        // Write out sector base of VBR
        uint32_t* sectorBase = (uint32_t*) &vbrBuf[92];
        *sectorBase = (vbrBase / IMG_SECT_SZ);
    }
    close (vbrFd);
    // Write out to disk
    if (pwrite (imgFd, vbrBuf, IMG_SECT_SZ * 2, vbrBase) == -1)
    {
        error ("%s:%s", file, strerror (errno));
        close (imgFd);
        return false;
    }
    close (imgFd);
    return true;
}

// Updates the MBR of a partition
bool updateMbr (Image_t* img)
{
    // Open up image
    const char* file = NULL;
    if (img->bootEmu == IMG_BOOTEMU_HDD && img->format == IMG_FORMAT_ISO9660)
        file = getenv ("NNBOOTIMG");
    else
        file = img->file;
    int imgFd = open (file, O_RDWR);
    if (imgFd == -1)
    {
        error ("%s:%s", file, strerror (errno));
        return false;
    }
    // Read in MBR
    uint8_t mbrBuf[IMG_SECT_SZ];
    if (pread (imgFd, mbrBuf, IMG_SECT_SZ, 0) == -1)
    {
        error ("%s: %s", file, strerror (errno));
        close (imgFd);
        return false;
    }
    // Get size of MBR
    struct stat st;
    if (stat (img->mbrFile, &st) == -1)
    {
        error ("%s: %s", img->mbrFile, strerror (errno));
        close (imgFd);
        return false;
    }
    if (st.st_size > IMG_SECT_SZ)
    {
        error ("%s: maximum size of MBR is %d bytes", img->mbrFile, IMG_SECT_SZ);
        close (imgFd);
        return false;
    }
    // Open up MBR file
    int mbrFd = open (img->mbrFile, O_RDONLY);
    if (mbrFd == -1)
    {
        error ("%s: %s", img->mbrFile, strerror (errno));
        close (imgFd);
        return false;
    }
    // Read into MBR buffer
    if (read (mbrFd, mbrBuf, st.st_size) == -1)
    {
        error ("%s:%s", img->mbrFile, strerror (errno));
        close (imgFd);
        close (mbrFd);
        return false;
    }
    // If this is a GPT disk, write out boot partition VBR base
    uint32_t* vbrBase = (uint32_t*) &mbrBuf[92];
    if (img->format == IMG_FORMAT_GPT)
        *vbrBase = IMG_MUL_TO_SECT (getBootPart (img)->start);
    close (mbrFd);
    // Write out to disk
    if (pwrite (imgFd, mbrBuf, IMG_SECT_SZ, 0) == -1)
    {
        error ("%s:%s", img->mbrFile, strerror (errno));
        close (imgFd);
        return false;
    }
    close (imgFd);
    return true;
}

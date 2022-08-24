/*
    image.c - contains functions to build images
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

/// @file image.c

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "nnimage.h"
#include <libnex.h>

// Host prefix directory
static const char* hostPrefix = NULL;

// Source root
static const char* scriptRoot = NULL;

// Boot image for El Torito images
static const char* bootImg = NULL;

// Alternate (i.e., EFI) boot image for El Torito
static const char* altBootImg = NULL;

// Current partition number
static int partNum = 0;

// Partition device being operated on
static char partDev[64] = {0};

// Converts multiplier value to sectors
#define IMG_MUL_TO_SECT(mulSz) (((mulSz) * (muls[img->mul])) / 512)

// Asks it the user wants to overwrite the image
static bool askOverwrite (const char* file)
{
    printf ("%s already exists, overwrite it? [y/N] ", file);
    int res = getc (stdin);
    if (res == 'y')
        return true;
    else
        return false;
}

static bool createImageInternal (const char* action,
                                 bool overwrite,
                                 const char* file,
                                 size_t mul,
                                 size_t sz)
{
    int fileNo = 0;
    struct stat st;
    // Check if file exists
    if ((stat (file, &st) == -1))
    {
        if (errno != ENOENT)
        {
            error ("%s: %s", file, strerror (errno));
            return false;
        }
    }
    else
    {
        if (strcmp (action, "create") != 0)
        {
            // Check size of file to see if its has already been created
            if (st.st_size == (muls[mul] * (long) sz))
                return true;
        }
        // Check if we need to ask the user if we should overwrite the file
        if (!overwrite)
            overwrite = askOverwrite (file);
        // Now overwrite or exit
        if (!overwrite)
            return false;
    }
    // Create the file
    fileNo = open (file, O_RDWR | O_CREAT | O_TRUNC, 0755);
    if (fileNo == -1)
    {
        error ("%s: %s", file, strerror (errno));
        return false;
    }
    // Write out as many zeroes as conf specified
    uint8_t* buf = malloc_s (muls[mul]);
    if (!buf)
        return false;
    memset (buf, 0, muls[mul]);
    for (int i = 0; i < sz; ++i)
    {
        if (write (fileNo, buf, muls[mul]) == -1)
        {
            error ("%s", strerror (errno));
            free (buf);
            return false;
        }
    }
    free (buf);
    close (fileNo);
    return true;
}

// Creates one image
static bool createImage (Image_t* img,
                         const char* action,
                         bool overwrite,
                         const char* file)
{
    // Check if the user passed a name
    if (file)
        img->file = (char*) file;
    // Was a name set in the configuration file?
    else if (!img->file)
    {
        error ("default image name not specified");
        return false;
    }
    // Create guestfs instance
    img->guestFs = guestfs_create();
    if (!img->guestFs)
        return false;
    if (!strcmp (action, "all") || !strcmp (action, "create"))
    {
        if (img->format != IMG_FORMAT_ISO9660)
            printf ("Creating image %s with size %u %s...\n",
                    img->name,
                    img->sz,
                    mulNames[img->mul]);
        else
            printf ("Creating ISO9660 image %s...\n", img->name);
        // If this is an ISO image, bail out
        if (img->format == IMG_FORMAT_ISO9660)
            return true;
        return createImageInternal (action, overwrite, img->file, img->mul, img->sz);
    }
    else
    {
        if (img->format != IMG_FORMAT_ISO9660)
        {
            // Check if image exists
            struct stat st;
            if (stat (img->file, &st) == -1)
            {
                error ("%s: %s\n", img->file, strerror (errno));
                return false;
            }
        }
        return true;
    }
}

// Cleans up FS stuff of one partition
static bool cleanPartition (const char* action, Image_t* img, Partition_t* part)
{
    if (!strcmp (action, "create") || !strcmp (action, "partition") ||
        part->filesys == IMG_FILESYS_ISO9660)
        return true;
    // Unmount in guestfs
    if (guestfs_umount (img->guestFs, "/mnt") == -1)
        return false;
    return true;
}

// Mounts a partition
static bool mountPartition (const char* action, Image_t* img, Partition_t* part)
{
    if (!strcmp (action, "create") || !strcmp (action, "partition") ||
        part->filesys == IMG_FILESYS_ISO9660)
        return true;
    // Create mount directory
    if (guestfs_mkdir_p (img->guestFs, "/mnt") == -1)
        return false;
    // Mount in guestfs
    if (guestfs_mount (img->guestFs, partDev, "/mnt") == -1)
        return false;
    return true;
}

// Formats a partition
static bool formatPartition (const char* action, Image_t* img, Partition_t* part)
{
    // Check if we need to do this
    if (!strcmp (action, "update") || !strcmp (action, "create") ||
        part->filesys == IMG_FILESYS_ISO9660)
        return true;
    printf ("Formatting partition %s with filesystem %s...\n",
            part->name,
            fsTypeNames[part->filesys]);
    // Device used by guestfs to represent disk to partition
    const char* guestFsDev = "/dev/sdb";
    if (part->isAltBootPart)
        guestFsDev = "/dev/sdc";
    // Handle ISO9660 boot partition case
    if (img->format == IMG_FORMAT_ISO9660)
    {
        if (!part->isBootPart && !part->isAltBootPart)
        {
            // Well this isn't an ISO9660 partition and this isn't a boot partition
            // Thats invalid
            error ("only boot partition of CD-ROM image can have a filesystem other "
                   "than ISO9660");
            return false;
        }
        // Ensure guestFsDev is in partDev so code will operate on it
        strcpy (partDev, guestFsDev);
    }
    else if (img->format != IMG_FORMAT_FLOPPY)
    {
        // Add partition to guestfs
        if (guestfs_part_add (img->guestFs,
                              guestFsDev,
                              "p",
                              IMG_MUL_TO_SECT (part->start),
                              (IMG_MUL_TO_SECT (part->start + part->sz) - 1)) == -1)
        {
            return false;
        }
        // Set file system type of partition
        if (img->format == IMG_FORMAT_MBR)
        {
            if (guestfs_part_set_mbr_id (img->guestFs,
                                         guestFsDev,
                                         partNum,
                                         mbrByteIds[part->filesys]) == -1)
                return false;
            // Set bootable flag if need be
            if (part->isBootPart)
                guestfs_part_set_bootable (img->guestFs, guestFsDev, partNum, true);
        }
        else if (img->format == IMG_FORMAT_GPT)
        {
            if (guestfs_part_set_gpt_type (img->guestFs,
                                           guestFsDev,
                                           partNum,
                                           gptGuids[part->filesys]) == -1)
                return false;
            if (guestfs_part_set_name (img->guestFs,
                                       guestFsDev,
                                       partNum,
                                       part->name) == -1)
                return false;
            // Handle bootable partitions
            if (part->isBootPart)
            {
                if (img->bootMode == IMG_BOOTMODE_BIOS)
                {
                    if (guestfs_part_set_bootable (img->guestFs,
                                                   guestFsDev,
                                                   partNum,
                                                   true) == -1)
                        return false;
                    if (guestfs_part_set_gpt_type (
                            img->guestFs,
                            guestFsDev,
                            partNum,
                            "21686148-6449-6E6F-744E-656564454649") == -1)
                    {
                        return false;
                    }
                }
                else
                {
                    if (guestfs_part_set_gpt_type (
                            img->guestFs,
                            guestFsDev,
                            partNum,
                            "C12A7328-F81F-11D2-BA4B-00A0C93EC93B") == -1)
                    {
                        return false;
                    }
                }
            }
        }
    }
#define CMDMAX 256
    // Format it
    char cmd[CMDMAX];
    if (part->filesys == IMG_FILESYS_FAT12)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t fat -F 12 -R 4 '");
        strcat (cmd, partDev);
        strcat (cmd, "'");
    }
    else if (part->filesys == IMG_FILESYS_FAT16)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t fat -F 16 -R 4 '");
        strcat (cmd, partDev);
        strcat (cmd, "'");
    }
    else if (part->filesys == IMG_FILESYS_FAT32)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t vfat -F 32 '");
        strcat (cmd, partDev);
        strcat (cmd, "'");
    }
    else if (part->filesys == IMG_FILESYS_EXT2)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t ext2 '");
        strcat (cmd, partDev);
        strcat (cmd, "'");
    }
    // Format it
    if (!guestfs_sh (img->guestFs, cmd))
        return false;
    return true;
}

// Child process ID
static pid_t childPid = 0;

// Redirects signals from parent process to shell
static void signalHandler (int signalNum)
{
    error ("child exiting on signal %d", signalNum);
    // Kill child process
    kill (childPid, signalNum);
}

// Runs specified script
static bool runScript (const char* script, const char** argv)
{
    pid_t pid = fork();
    if (!pid)
    {
        execv (script, (char* const*) argv);
        error ("%s\n", strerror (errno));
        // An error occured if we got here
        _Exit (1);
    }
    else
    {
        childPid = pid;
        // Handle signals
        signal (SIGINT, signalHandler);
        signal (SIGQUIT, signalHandler);
        signal (SIGHUP, signalHandler);
        signal (SIGTERM, signalHandler);
        // Wait for the shell to finish
        int status = 0;
        wait (&status);
        // Exit if an error occured
        if (status)
        {
            error ("an error occurred while writing the CD-ROM image");
            return false;
        }
    }
    return true;
}

// Runs writeiso.sh to create ISO9660 image
bool writeIso (Image_t* img)
{
    // Prepare script name
    const char* script = "writeiso.sh";
    char* scriptPath = malloc_s (strlen (script) + strlen (scriptRoot) + 1);
    strcpy (scriptPath, scriptRoot);
    strcat (scriptPath, script);
    // Make sure an MBR was passed if we need one
    if ((img->bootMode == IMG_BOOTMODE_HYBRID ||
         img->bootMode == IMG_BOOTMODE_BIOS) &&
        img->isUniversal)
    {
        if (!img->mbrFile)
        {
            error ("MBR file must be passed to universal BIOS or hybrid images");
            return false;
        }
    }
    // Prepare argv
    static const char* argv[10] = {0};
    argv[0] = script;
    argv[1] = img->file;
    // TODO: allow xorriso list file to be dynamically changed
    argv[2] = "xorrisolst.txt";
    // FIXME: memory leak
    if (bootImg)
        argv[3] = basename (strdup (bootImg));
    argv[4] = bootModeNames[img->bootMode];
    argv[5] = bootEmuNames[img->bootEmu];
    argv[6] = (img->isUniversal) ? "true" : "false";
    argv[7] = img->mbrFile;
    if (altBootImg)
        argv[8] = basename (strdup (altBootImg));
    argv[9] = NULL;
    // Run it
    if (!runScript (scriptPath, argv))
        return false;
    return true;
}

bool createImages (ListHead_t* images,
                   const char* action,
                   bool overwrite,
                   const char* file,
                   const char* listFile)
{
    // Get host prefix
    hostPrefix = getenv ("NNDESTDIR");
    if (!hostPrefix)
    {
        error ("variable NNDESTDIR must be set");
        return false;
    }
    // Get script root
    scriptRoot = getenv ("NNSCRIPTROOT");
    if (!scriptRoot)
    {
        error ("variable NNSCRIPTROOT must be set");
        return false;
    }
    // Get path of root image
    char rootImage[256];
    // We reserve space for file name
    if (strlcpy (rootImage, scriptRoot, 256) >= (256 - 17))
    {
        error ("buffer overflow detected");
        return false;
    }
    strcat (rootImage, "guestfs_root.img");
    // Loop through every image
    ListEntry_t* imgEntry = ListFront (images);
    while (imgEntry)
    {
        Image_t* img = ListEntryData (imgEntry);
        // Sanity checks
        // Ensure a partition format was specified
        if (!img->format)
        {
            error ("partition table format not specified on image %s", img->name);
            goto nextImg;
        }
        if (!img->sz &&
            (img->format != IMG_FORMAT_FLOPPY && img->format != IMG_FORMAT_ISO9660))
        {
            error ("image size not set on image %s", img->name);
            goto nextImg;
        }
        // Set default multiplier. KiB on floppies, MiB elsewhere
        if (!img->mul)
        {
            if (img->format == IMG_FORMAT_FLOPPY)
                img->mul = IMG_MUL_KIB;
            else
                img->mul = IMG_MUL_MIB;
        }
        // Ensure that a boot emulation was specified only on ISO9660
        if (!img->bootMode)
            img->bootMode = IMG_BOOTMODE_NOBOOT;
        // Ensure partition count for MBR is less than 4
        if (img->format == IMG_FORMAT_MBR)
        {
            if (img->partCount > 4)
            {
                error ("partition count > 4 not allowed on MBR disks!");
                goto nextImg;
            }
        }
        else if (img->format == IMG_FORMAT_ISO9660)
        {
            if (!img->bootEmu)
                img->bootEmu = IMG_BOOTEMU_NONE;
        }
        // Check if we need a bootable partition
        if (img->bootMode != IMG_BOOTMODE_NOBOOT)
        {
            if (!getBootPart (img))
            {
                error ("bootable partition not found on image %s", img->name);
                goto nextImgNoClean;
            }
            // Check if we need an alternate boot partition
            if (img->format == IMG_FORMAT_ISO9660 &&
                img->bootMode == IMG_BOOTMODE_HYBRID && !getAltBootPart (img))
            {
                error ("alternate boot partition not found on image %s", img->name);
                goto nextImgNoClean;
            }
        }
        if (!createImage (img, action, overwrite, file))
            goto nextImgNoClean;
        // Add root filesystem
        if (guestfs_add_drive (img->guestFs, rootImage) == -1)
            goto nextImg;
        // Add image to guestfs handle
        if (img->format != IMG_FORMAT_ISO9660)
        {
            if (guestfs_add_drive (img->guestFs, img->file) == -1)
                goto nextImg;
        }
        else
        {
            // Check if image needs a temp boot image
            if (getBootPart (img))
            {
                // Create temporary image
                bootImg = getenv ("NNBOOTIMG");
                if (!bootImg)
                {
                    error ("NNBOOTIMG not set in environment");
                    goto nextImg;
                }
                if (!createImageInternal (action,
                                          true,
                                          bootImg,
                                          img->mul,
                                          getBootPart (img)->sz))
                    goto nextImg;
                // Add to guestfs
                if (guestfs_add_drive (img->guestFs, bootImg) == -1)
                    goto nextImg;
            }
            if (img->bootMode == IMG_BOOTMODE_HYBRID &&
                img->format == IMG_FORMAT_ISO9660)
            {
                // Make sure an alt boot partition exists
                if (!getAltBootPart (img))
                {
                    error ("alternate boot partition required on hybrid ISO9660 "
                           "images");
                    goto nextImg;
                }
                // Do the same as above
                altBootImg = getenv ("NNALTBOOTIMG");
                if (!altBootImg)
                {
                    error ("NNALTBOOTIMG not set in environment");
                    goto nextImg;
                }
                if (!createImageInternal (action,
                                          true,
                                          altBootImg,
                                          img->mul,
                                          getAltBootPart (img)->sz))
                    return false;
                // Add to guestfs
                if (guestfs_add_drive (img->guestFs, altBootImg) == -1)
                    goto nextImg;
            }
        }
        if (guestfs_launch (img->guestFs) == -1)
            goto nextImg;
        // Mount root
        if (guestfs_mount (img->guestFs, "/dev/sda3", "/") == -1)
            goto nextImg;
        // CHeck if a partition table needs to be created
        if (!strcmp (action, "partition") || !strcmp (action, "all"))
        {
            if (img->format != IMG_FORMAT_FLOPPY &&
                img->format != IMG_FORMAT_ISO9660)
            {
                // Create a new partition table
                if (guestfs_part_init (img->guestFs,
                                       "/dev/sdb",
                                       partTypeNames[img->format]) == -1)
                    goto nextImg;
            }
        }
        ListEntry_t* partEntry = ListFront (img->partsList);
        while (partEntry)
        {
            ++partNum;
            Partition_t* part = ListEntryData (partEntry);
            // Set partition device
            if (!part->isAltBootPart)
                strcpy (partDev, "/dev/sdb");
            else
                strcpy (partDev, "/dev/sdc");
            if (img->format != IMG_FORMAT_FLOPPY &&
                img->format != IMG_FORMAT_ISO9660)
            {
                // Convert number to ASCII
                int numSize = sprintf (partDev + 8, "%d", partNum);
                partDev[numSize + 8] = 0;
            }
            // Check required fields
            if (!part->prefix)
            {
                error ("prefix not specified on partition %s", part->name);
                goto nextPart;
            }

            if (img->format == IMG_FORMAT_FLOPPY ||
                (img->format == IMG_FORMAT_ISO9660 &&
                 img->bootEmu == IMG_BOOTEMU_FDD && part->isBootPart))
            {
                if (img->format == IMG_FORMAT_FLOPPY)
                {
                    // Ensure we have only 1 partition on floppies
                    if (img->partCount != 1)
                    {
                        error ("floppy image %s has more then 1 partition specified",
                               img->name);
                        goto nextPart;
                    }
                    if (img->mul != IMG_MUL_KIB)
                    {
                        error ("floppy image %s using multiplier other KiB",
                               img->name);
                        goto nextPart;
                    }
                    // Check that the size is 720K, 1.44M, or 2.88M
                    if (img->sz != 720 && img->sz != 1440 && img->sz != 2880)
                    {
                        error ("floppy image %s doesn't have a size of either 720, "
                               "1440, or 2880",
                               img->name);
                        goto nextPart;
                    }
                    img->bootMode = IMG_BOOTMODE_BIOS;
                }
                else
                {
                    if (img->mul != IMG_MUL_KIB)
                    {
                        error (
                            "CD-ROM with floppy image %s using multiplier other KiB",
                            img->name);
                        goto nextPart;
                    }
                    // Check that the size is 720K, 1.44M, or 2.88M
                    if (part->sz != 720 && part->sz != 1440 && part->sz != 2880)
                    {
                        error ("floppy image %s doesn't have a size of either 720, "
                               "1440, or 2880",
                               img->name);
                        goto nextPart;
                    }
                }
                part->filesys = IMG_FILESYS_FAT12;
            }
            else if (img->format == IMG_FORMAT_ISO9660)
            {
                if (!part->filesys)
                {
                    error ("file system type not specified on partition %s",
                           part->name);
                    goto nextPart;
                }
                if (part->filesys == IMG_FILESYS_FAT12)
                {
                    error ("FAT12 not allowed on CD-ROMs");
                    goto nextPart;
                }
                else if (part->filesys == IMG_FILESYS_EXT2)
                {
                    error ("ext2 not allowed on CD-ROMs");
                    goto nextPart;
                }
                else if (part->filesys != IMG_FILESYS_ISO9660)
                {
                    if (!part->sz)
                    {
                        error ("bounds not specified on partition %s", part->name);
                        goto nextPart;
                    }
                }
            }
            else
            {
                if (!part->filesys)
                {
                    error ("file system type not specified on partition %s",
                           part->name);
                    goto nextPart;
                }
                if (part->filesys == IMG_FILESYS_FAT12)
                {
                    error ("FAT12 not allowed on hard disks");
                    goto nextPart;
                }
                if (!part->start || !part->sz)
                {
                    error ("bounds not specified on partition %s", part->name);
                    goto nextPart;
                }
            }
            // Go through partitions
            if (!strcmp (action, "all") || !strcmp (action, "partition"))
            {
                // Format partition
                if (!formatPartition (action, img, part))
                    goto nextPart;
                // Clean up partition file system data
                if (strcmp (action, "all") != 0)
                {
                    if (!cleanPartition (action, img, part))
                        goto nextPart;
                }
                else
                    goto update;
            }
            else if (!strcmp (action, "update"))
            {
            update:
                // Ensure a list file was specified
                if (!listFile)
                {
                    error ("list file not specified on command line");
                    goto nextImg;
                }
                // Mount the partition
                if (!mountPartition (action, img, part))
                    goto nextPart;
                if (!updatePartition (img, part, listFile, "/mnt", hostPrefix))
                    goto nextPart;
                // Clean up partition file system data
                if (!cleanPartition (action, img, part))
                    goto nextPart;
            }
            else if (!strcmp (action, "create"))
                ;
            else
            {
                error ("invalid action \"%s\"", action);
                return false;
            }
        nextPart:
            partEntry = ListIterate (partEntry);
        }
    nextImg:
        // Destroy handle
        if (img->guestFs)
        {
            guestfs_shutdown (img->guestFs);
            guestfs_close (img->guestFs);
        }
        // Decide if we should write out the VBR and MBR
        if (!strcmp (action, "update") || !strcmp (action, "all"))
        {
            // Check if this image needs a VBR or MBR
            if (img->bootMode == IMG_BOOTMODE_HYBRID ||
                img->bootMode == IMG_BOOTMODE_BIOS)
            {
                if (img->format != IMG_FORMAT_FLOPPY)
                {
                    if (!getBootPart (img)->vbrFile)
                    {
                        error (
                            "\"vbrFile\" property not set on BIOS bootable image");
                        return false;
                    }
                }
                else
                {
                    if (!img->mbrFile)
                    {
                        error (
                            "\"mbrFile\" property not set on BIOS bootable image");
                        return false;
                    }
                    getBootPart (img)->vbrFile = strdup (img->mbrFile);
                }
                if (!updateVbr (img, getBootPart (img)))
                {
                    imgEntry = ListIterate (imgEntry);
                    continue;
                }
                // Check if we need to write out MBR
                if (img->format != IMG_FORMAT_ISO9660 &&
                    img->format != IMG_FORMAT_FLOPPY)
                {
                    if (!img->mbrFile)
                    {
                        error ("\"mbrFile\" property not set on BIOS bootable hard "
                               "disk image");
                        return false;
                    }
                    // Write it out
                    if (!updateMbr (img))
                    {
                        imgEntry = ListIterate (imgEntry);
                        continue;
                    }
                }
            }
        }
        // If this is an ISO image, write it out if the action is update
        if (img->format == IMG_FORMAT_ISO9660 &&
            (!strcmp (action, "update") || !strcmp (action, "all")))
        {
            writeIso (img);
        }
    nextImgNoClean:
        imgEntry = ListIterate (imgEntry);
    }
    return true;
}

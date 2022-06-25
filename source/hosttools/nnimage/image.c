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

#include <conf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nnimage.h"
#include <libnex.h>

// Host prefix directory
static const char* hostPrefix = NULL;

// Source root
static const char* scriptRoot = NULL;

// Current partition number
static int partNum = 0;

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

// Creates one image
static bool createImage (Image_t* img, const char* action, bool overwrite, const char* file)
{
    // Check if the user passed a name
    if (file)
        img->file = (char*) file;
    // Was a name set in the configuration file?
    else if (!img->file)
    {
        error ("%s: default image name not specified", ConfGetFileName());
        return false;
    }
    // Create guestfs instance
    img->guestFs = guestfs_create();
    if (!img->guestFs)
        return false;
    // If this is an ISO image, bail out
    if (img->format == IMG_FORMAT_ISO9660)
        return true;
    int fileNo = 0;
    struct stat st;
    // If action is all, decide if we need to create a new image
    if (!strcmp (action, "all") || !strcmp (action, "create"))
    {
        // Check if file exists
        if ((stat (img->file, &st) == -1))
        {
            if (errno != ENOENT)
            {
                error ("%s", strerror (errno));
                return false;
            }
        }
        else
        {
            if (strcmp (action, "create") != 0)
            {
                // Check size of file to see if its has already been created
                if (st.st_size == (muls[img->mul] * (long) img->sz))
                    goto openImg;
            }
            // Check if we need to ask the user if we should overwrite the file
            if (!overwrite)
                overwrite = askOverwrite (img->file);
            // Now overwrite or exit
            if (!overwrite)
                return false;
        }
        printf ("Creating image %s with size %u %s...\n", img->name, img->sz, mulNames[img->mul]);
        // Create the file
        fileNo = open (img->file, O_RDWR | O_CREAT | O_TRUNC, 0755);
        if (fileNo == -1)
        {
            error ("%s", strerror (errno));
            return false;
        }
        // Write out as many zeroes as conf specified
        uint8_t* buf = malloc_s (muls[img->mul]);
        if (!buf)
            return false;
        memset (buf, 0, muls[img->mul]);
        for (int i = 0; i < img->sz; ++i)
        {
            if (write (fileNo, buf, muls[img->mul]) == -1)
            {
                error ("%s", strerror (errno));
                free (buf);
                return false;
            }
        }
        free (buf);
    }
    else
    {
        // Check if image exists
        if (stat (img->file, &st) == -1)
        {
            error ("%s", strerror (errno));
            return false;
        }
    openImg:
        fileNo = open (img->file, O_RDWR);
        if (fileNo == -1)
        {
            error ("%s", strerror (errno));
            return false;
        }
    }
    img->fileNo = fileNo;
    return true;
}

// Cleans up FS stuff of one partition
static bool cleanPartition (const char* action, Image_t* img, Partition_t* part)
{
    if (!strcmp (action, "create") || !strcmp (action, "partition"))
        return true;
    // Check if this partition is an ISO9660 partition
    if (part->filesys == IMG_FILESYS_ISO9660)
    {
        // Ensure it's on a ISO disk
        if (img->format != IMG_FORMAT_ISO9660)
        {
            error ("ISO9660 partition must be on an ISO disk image");
            return false;
        }
    }

    return true;
}

// Mounts a partition
static bool mountPartition (const char* action, Image_t* img, Partition_t* part)
{
    if (!strcmp (action, "create") || !strcmp (action, "partition") || part->filesys == IMG_FILESYS_ISO9660)
        return true;
    return true;
}

// Formats a partition
static bool formatPartition (const char* action, Image_t* img, Partition_t* part)
{
    // Check if we need to do this
    if (!strcmp (action, "update") || !strcmp (action, "create") || part->filesys == IMG_FILESYS_ISO9660)
        return true;
#define DEVMAX 45
    // Device to format
    char devToFormat[DEVMAX];
    if (img->format != IMG_FORMAT_FLOPPY)
    {
        // Add partition to guestfs
        if (guestfs_part_add (img->guestFs,
                              "/dev/sda",
                              "p",
                              IMG_MUL_TO_SECT (part->start),
                              (IMG_MUL_TO_SECT (part->start + part->sz) - 1)) == -1)
        {
            return false;
        }
        // Set file system type of partition
        if (img->format == IMG_FORMAT_MBR)
        {
            if (guestfs_part_set_mbr_id (img->guestFs, "/dev/sda", partNum, mbrByteIds[part->filesys]) == -1)
                return false;
            // Set bootable flag if need be
            if (part->isBootPart)
                guestfs_part_set_bootable (img->guestFs, "/dev/sda", partNum, true);
        }
        else if (img->format == IMG_FORMAT_GPT)
        {
            if (guestfs_part_set_gpt_type (img->guestFs, "/dev/sda", partNum, gptGuids[part->filesys]) == -1)
                return false;
            if (guestfs_part_set_name (img->guestFs, "/dev/sda", partNum, part->name) == -1)
                return false;
            // Handle bootable partitions
            if (part->isBootPart)
            {
                if (img->bootMode == IMG_BOOTMODE_BIOS)
                {
                    if (guestfs_part_set_bootable (img->guestFs, "/dev/sda", partNum, true) == -1)
                        return false;
                    if (guestfs_part_set_gpt_type (img->guestFs,
                                                   "/dev/sda",
                                                   partNum,
                                                   "21686148-6449-6E6F-744E-656564454649") == -1)
                    {
                        return false;
                    }
                }
                else
                {
                    if (guestfs_part_set_gpt_type (img->guestFs,
                                                   "/dev/sda",
                                                   partNum,
                                                   "C12A7328-F81F-11D2-BA4B-00A0C93EC93B") == -1)
                    {
                        return false;
                    }
                }
            }
        }
        // Format it
        strcpy (devToFormat, "/dev/sda");
        // Convert number to ASCII
        int numSize = sprintf (devToFormat + 8, "%d", partNum);
        devToFormat[numSize + 8] = 0;
    }
    else
        strcpy (devToFormat, "/dev/sda");
#define CMDMAX 256
    // Format it
    const char* fsType = NULL;
    char cmd[CMDMAX];
    if (part->filesys == IMG_FILESYS_FAT12)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t vfat -F 12 '");
        strcat (cmd, devToFormat);
        strcat (cmd, "'");
    }
    else if (part->filesys == IMG_FILESYS_FAT16)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t vfat -F 16 '");
        strcat (cmd, devToFormat);
        strcat (cmd, "'");
    }
    else if (part->filesys == IMG_FILESYS_FAT32)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t vfat -F 32 '");
        strcat (cmd, devToFormat);
        strcat (cmd, "'");
    }
    else if (part->filesys == IMG_FILESYS_EXT2)
    {
        // Bulid command string
        strcpy (cmd, "mkfs -t ext2 '");
        strcat (cmd, devToFormat);
        strcat (cmd, "'");
    }
    // Format it
    if (!guestfs_sh (img->guestFs, cmd))
        return false;
    return true;
}

bool createImages (ListHead_t* images, const char* action, bool overwrite, const char* file, const char* listFile)
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
        if (!img->sz)
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
        if (img->format == IMG_FORMAT_ISO9660 && !img->bootEmu)
            img->bootEmu = IMG_BOOTEMU_NONE;
        else if (img->format != IMG_FORMAT_ISO9660 && img->bootEmu)
        {
            error ("boot emulation only valid for ISO9660 images");
            goto nextImg;
        }
        if (!img->bootMode)
            img->bootMode = IMG_BOOTMODE_HYBRID;
        // Ensure partition count for MBR is less than 4
        if (img->format == IMG_FORMAT_MBR)
        {
            if (img->partCount > 4)
            {
                error ("partition count > 4 not allowed on MBR disks! ");
                goto nextImg;
            }
        }
        if (!createImage (img, action, overwrite, file))
            goto nextImg;
        close (img->fileNo);
        // Add image to guestfs handle
        if (guestfs_add_drive (img->guestFs, img->file) == -1)
            goto nextImg;
        // Add root filesystem
        if (guestfs_add_drive (img->guestFs, rootImage) == -1)
            goto nextImg;
        if (guestfs_launch (img->guestFs) == -1)
            goto nextImg;
        // Mount root
        if (guestfs_mount (img->guestFs, "/dev/sdb3", "/") == -1)
            goto nextImg;
        // CHeck if a partition table needs to be created
        if (!strcmp (action, "partition") || !strcmp (action, "all"))
        {
            // Create a new partition table
            if (guestfs_part_init (img->guestFs, "/dev/sda", partTypeNames[img->format]) == -1)
                goto nextImg;
        }
        ListEntry_t* partEntry = ListFront (img->partsList);
        while (partEntry)
        {
            ++partNum;
            Partition_t* part = ListEntryData (partEntry);
            // Go through partitions
            if (!strcmp (action, "all") || !strcmp (action, "partition"))
            {
                // Check required fields
                if (!part->prefix && !part->isRootPart)
                {
                    error ("prefix not specified on partition %s", part->name);
                    goto nextPart;
                }
                if (img->format != IMG_FORMAT_FLOPPY)
                {
                    if (!part->filesys)
                    {
                        error ("file system type not specified on partition %s", part->name);
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
                else
                {
                    // Ensure we have only 1 partition on floppies
                    if (img->partCount != 1)
                    {
                        error ("floppy image %s has more then 1 partition specified", img->name);
                        goto nextPart;
                    }
                    if (img->mul != IMG_MUL_KIB)
                    {
                        error ("floppy image %s using multiplier other KiB", img->name);
                        goto nextPart;
                    }
                    // Check that the size is 720K, 1.44M, or 2.88M
                    if (img->sz != 720 && img->sz != 1440 && img->sz != 2880)
                    {
                        error ("floppy image %s doesn't have a size of either 720, 1440, or 2880", img->name);
                        goto nextPart;
                    }
                    if (part->filesys && part->filesys != IMG_FILESYS_FAT12)
                    {
                        error ("only FAT12 is supported on floppy disks");
                        goto nextPart;
                    }
                    else
                        part->filesys = IMG_FILESYS_FAT12;
                }
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
                    return false;
                }
                // Mount the partition
                if (!mountPartition (action, img, part))
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
            guestfs_umount (img->guestFs, "/dev/sdb3");
            guestfs_shutdown (img->guestFs);
            guestfs_close (img->guestFs);
        }
        // Cleanup and commit partition data
        imgEntry = ListIterate (imgEntry);
    }
    return true;
}

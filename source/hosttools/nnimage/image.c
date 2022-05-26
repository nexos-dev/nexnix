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
#include <unistd.h>

#include "nnimage.h"
#include <libnex.h>
#include <sys/stat.h>

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
                if (st.st_size == (img->mul * img->sz))
                    goto openImg;
            }
            // Check if we need to ask the user if we should overwrite the file
            if (!overwrite)
                overwrite = askOverwrite (img->file);
            // Now overwrite or exit
            if (!overwrite)
                return false;
        }
        // Create the file
        fileNo = open (img->file, O_RDWR | O_CREAT | O_TRUNC, 0755);
        if (fileNo == -1)
        {
            error ("%s", strerror (errno));
            return false;
        }
        // Write out as many zeroes as conf specified
        uint8_t* buf = malloc_s (img->mul);
        if (!buf)
            return false;
        memset (buf, 0, img->mul);
        for (int i = 0; i < img->sz; ++i)
        {
            if (write (fileNo, buf, img->mul) == -1)
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
    openImg:
        // Check if image exists
        if (stat (img->file, &st) == -1)
        {
            error ("%s", strerror (errno));
            return false;
        }
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

// Creates partition table structure
static bool createPartitionTable (Image_t* img, const char* action)
{
    if (!img->format)
    {
        error ("%s: partition table format not specified", ConfGetFileName());
        return false;
    }
    // Check if we need to do this
    if (!(!strcmp (action, "all") || !strcmp (action, "partition")))
        return true;

    // Decide what layer to call
    if (img->format == IMG_FORMAT_MBR)
    {
        if (!createMbr (img))
            return false;
    }
    else if (img->format == IMG_FORMAT_FLOPPY)
        ;    // There is nothing to do
    else if (img->format == IMG_FORMAT_GPT)
    {
        if (!createGpt (img))
            return false;
    }
    else if (img->format == IMG_FORMAT_ISO9660)
    {
    }
    return true;
}

// Clean up partition table layer
static bool cleanPartLayer (const char* action, Image_t* img)
{
    if (!strcmp (action, "partition") || !strcmp (action, "all"))
    {
        // Cleanup partition layer
        if (img->format == IMG_FORMAT_MBR)
        {
            if (!cleanupMbr (img))
                return false;
        }
        else if (img->format == IMG_FORMAT_GPT)
        {
            if (!cleanupGpt (img))
                return false;
        }
    }
    return true;
}

// Cleans up FS stuff of one partition
static bool cleanPartition (const char* action, Image_t* img, Partition_t* part)
{
    // Detect file system
    if (part->filesys == IMG_FILESYS_FAT12 || part->filesys == IMG_FILESYS_FAT16 ||
        part->filesys == IMG_FILESYS_FAT32)
    {
        return cleanupFat (img, part);
    }
    return true;
}

// Mounts a partition
static bool mountPartition (Image_t* img, Partition_t* part)
{
    if (img->format == IMG_FORMAT_GPT)
        mountGptPartition (img, part);
    else if (img->format == IMG_FORMAT_MBR)
        mountMbrPartition (img, part);

    // Mount file system
    if (part->filesys == IMG_FILESYS_FAT12 || part->filesys == IMG_FILESYS_FAT16 ||
        img->format == IMG_FORMAT_FLOPPY)
    {
        if (!mountFat (img, part))
            return false;
    }
    return true;
}

bool createImages (ListHead_t* images, const char* action, bool overwrite, const char* file)
{
    // Loop through every image
    ListEntry_t* imgEntry = ListFront (images);
    while (imgEntry)
    {
        Image_t* img = ListEntryData (imgEntry);
        // Sanity checks

        // Ensure size of sector is less than or equal to size of multiplier, as this program
        // is written to assume that
        if (img->sectSz && (img->sectSz > img->mul))
        {
            error ("sector size of image %s is greater than multiplier size", img->name);
            return false;
        }
        // Ensure a partition format was specified
        if (!img->format)
        {
            error ("partition table format not specified on image %s", img->name);
            return false;
        }
        if (!img->sz)
        {
            error ("image size not set on image %s", img->name);
            return false;
        }
        // Set default multiplier. KiB on floppies, MiB elsewhere
        if (!img->mul)
        {
            if (img->format == IMG_FORMAT_FLOPPY)
                img->mul = 1024;
            else
                img->mul = 1024 * 1024;
        }
        if (!createImage (img, action, overwrite, file))
            return false;
        // Create partition table
        if (!createPartitionTable (img, action))
            return false;
        ListEntry_t* partEntry = ListFront (img->partsList);
        while (partEntry)
        {
            Partition_t* part = ListEntryData (partEntry);
            // Go through partitions
            if (!strcmp (action, "all") || !strcmp (action, "partition"))
            {
                // Check required fields
                if (!part->prefix)
                {
                    error ("prefix not specified on partition %s", part->name);
                    cleanPartLayer (action, img);
                    return false;
                }
                if (img->format != IMG_FORMAT_FLOPPY)
                {
                    if (!part->filesys)
                    {
                        error ("file system type not specified on partition %s", part->name);
                        cleanPartLayer (action, img);
                        return false;
                    }
                    if (!part->start || !part->sz)
                    {
                        error ("bounds not specified on partition %s", part->name);
                        cleanPartLayer (action, img);
                        return false;
                    }
                }
                if (img->format == IMG_FORMAT_MBR)
                {
                    if (!addMbrPartition (img, part))
                    {
                        cleanPartLayer (action, img);
                        return false;
                    }
                }
                else if (img->format == IMG_FORMAT_GPT)
                {
                    if (!addGptPartition (img, part))
                    {
                        cleanPartLayer (action, img);
                        return false;
                    }
                }
                else if (img->format == IMG_FORMAT_FLOPPY)
                {
                    // Ensure we have only 1 partition on floppies
                    if (img->partCount != 1)
                    {
                        error ("floppy image %s has more then 1 partition specified", img->name);
                        return false;
                    }
                    if (img->mul != 1024)
                    {
                        error ("floppy image %s using multiplier other KiB", img->name);
                        return false;
                    }
                    // Check that the size is 720K, 1.44M, or 2.88M
                    if (img->sz != 720 && img->sz != 1440 && img->sz != 2880)
                    {
                        error ("floppy image %s doesn't have a size of either 720, 1440, or 2880", img->name);
                        return false;
                    }
                    // Format it
                    if (!formatFatFloppy (img, part))
                        return false;
                }
                // Clean up partitionn file system data
                if (!cleanPartition (action, img, part))
                {
                    cleanPartLayer (action, img);
                    return false;
                }
            }
            else if (!strcmp (action, "update"))
            {
                // Mount the partition
                if (!mountPartition (img, part))
                    return false;

                // Clean up partitionn file system data
                if (!cleanPartition (action, img, part))
                    return false;
            }
            else if (!strcmp (action, "create"))
                ;
            else
            {
                close (img->fileNo);
                error ("invalid action \"%s\"", action);
                return false;
            }
            partEntry = ListIterate (partEntry);
        }
        // Cleanup and commit partition data
        if (!cleanPartLayer (action, img))
            return false;

        close (img->fileNo);
        imgEntry = ListIterate (imgEntry);
    }
    return true;
}

bool writeSector (Image_t* img, void* data, uint32_t sector)
{
    lseek (img->fileNo, (sector * img->sectSz), SEEK_SET);
    if (write (img->fileNo, data, img->sectSz) == -1)
    {
        error ("%s", strerror (errno));
        return false;
    }
    return true;
}

bool readSector (Image_t* img, void* buf, uint32_t sector)
{
    lseek (img->fileNo, (sector * img->sectSz), SEEK_SET);
    if (read (img->fileNo, buf, img->sectSz) == -1)
    {
        error ("%s\n", strerror (errno));
        return false;
    }
    return true;
}

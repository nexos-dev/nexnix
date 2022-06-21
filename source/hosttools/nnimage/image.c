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
#include <sys/wait.h>
#include <unistd.h>

#include "nnimage.h"
#include <libnex.h>

// Child process ID
static pid_t childPid = 0;

// Script dirname
static char* scriptDirName = NULL;

// End of scriptDirName
static size_t dirNameSz = 0;

// Mounting directory
static const char* mountDir = NULL;

// Host prefix
static const char* hostPrefix = NULL;

// Current partition number
static int partNum = -1;

#define MAX_SCRIPTSZ 33

// Redirects signals from parent process to shell
static void signalHandler (int signalNum)
{
    error ("child exiting on signal %d", signalNum);
    // Redirect to child
    kill (childPid, signalNum);
}

// Runs a script
static bool runScript (char* cmd, const char* action, const char** argv)
{
    pid_t pid = fork();
    if (!pid)
    {
        execv (cmd, (char* const*) argv);
        // An error occured if we got here
        _Exit (1);
    }
    else
    {
        childPid = pid;
        // Handle signals
        (void) signal (SIGINT, signalHandler);
        (void) signal (SIGQUIT, signalHandler);
        (void) signal (SIGHUP, signalHandler);
        (void) signal (SIGTERM, signalHandler);
        // Wait for the script to finish
        int status = 0;
        wait (&status);
        // Exit if an error occured
        if (status)
        {
            error ("an error occured while invoking action \"%s\"", action);
            return false;
        }
    }
    return true;
}

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

// Creates partition table structure
static bool createPartitionTable (Image_t* img, const char* action)
{
    // Check if we need to do this
    if (!strcmp (action, "update") || !strcmp (action, "partition"))
        return true;
    // Check if image format has a partition table like this
    if (img->format == IMG_FORMAT_ISO9660 || img->format == IMG_FORMAT_FLOPPY)
        return true;
    // Concatenate script for this part
    if (strlcat (scriptDirName, "parttab.sh", dirNameSz + MAX_SCRIPTSZ) >= (dirNameSz + MAX_SCRIPTSZ))
    {
        scriptDirName[dirNameSz] = 0;
        error ("buffer overflow");
        return false;
    }
    // Prepare argv for this
    const char* argv[5];
    argv[0] = scriptDirName;
    argv[1] = partTypeNames[img->format];
    argv[2] = img->file;
    argv[3] = getprogname();
    argv[4] = NULL;
    // Run the script
    if (!runScript (scriptDirName, action, argv))
    {
        scriptDirName[dirNameSz] = 0;
        return false;
    }
    scriptDirName[dirNameSz] = 0;
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
    // Concatenate script for this part
    if (strlcat (scriptDirName, "umountpart.sh", dirNameSz + MAX_SCRIPTSZ) >= (dirNameSz + MAX_SCRIPTSZ))
    {
        scriptDirName[dirNameSz] = 0;
        error ("buffer overflow");
        return false;
    }
    // Setup argv
    const char* argv[6];
    argv[0] = scriptDirName;
    argv[1] = getprogname();
    argv[2] = img->file;
    argv[3] = partTypeNames[img->format];
    argv[4] = fsTypeNames[part->filesys];
    argv[5] = NULL;
    // Run the script
    if (!runScript (scriptDirName, action, argv))
    {
        scriptDirName[dirNameSz] = 0;
        return false;
    }
    scriptDirName[dirNameSz] = 0;
    return true;
}

// Mounts a partition
static bool mountPartition (const char* action, Image_t* img, Partition_t* part)
{
    if (!strcmp (action, "create") || !strcmp (action, "partition"))
        return true;
    // Concatenate script for this part
    if (strlcat (scriptDirName, "mountpart.sh", dirNameSz + MAX_SCRIPTSZ) >= (dirNameSz + MAX_SCRIPTSZ))
    {
        scriptDirName[dirNameSz] = 0;
        error ("buffer overflow");
        return false;
    }
    // Setup argv
    const char* argv[7];
    argv[0] = scriptDirName;
    argv[1] = getprogname();
    argv[2] = partTypeNames[img->format];
    argv[3] = img->file;
    // Set partition number
    char partNumS[20];
    (void) snprintf (partNumS, 20, "%d", partNum);
    argv[4] = partNumS;
    argv[5] = fsTypeNames[part->filesys];
    argv[6] = NULL;
    // Run the script
    if (!runScript (scriptDirName, action, argv))
    {
        scriptDirName[dirNameSz] = 0;
        return false;
    }
    scriptDirName[dirNameSz] = 0;
    return true;
}

// Formats a partition
static bool formatPartition (const char* action, Image_t* img, Partition_t* part)
{
    // Check if we need to do this
    if (!strcmp (action, "update") || !strcmp (action, "create"))
        return true;
    // Concatenate script for this part
    if (strlcat (scriptDirName, "createpart.sh", dirNameSz + MAX_SCRIPTSZ) >= (dirNameSz + MAX_SCRIPTSZ))
    {
        scriptDirName[dirNameSz] = 0;
        error ("buffer overflow");
        return false;
    }
    // Prepare argv for this
    const char* argv[15];
    argv[0] = scriptDirName;
    argv[1] = partTypeNames[img->format];
    argv[2] = fsTypeNames[part->filesys];
    argv[3] = img->file;
    argv[4] = bootModeNames[img->bootMode];
    argv[5] = bootEmuNames[img->bootEmu];
    argv[6] = (part->isBootPart) ? "1" : "0";
    argv[7] = part->name;
    // Set partition base
    char partStart[255];
    (void) snprintf (partStart, 255, "%u", part->start);
    argv[8] = partStart;
    // Set partition size simillarly
    char partSize[255];
    (void) snprintf (partSize, 255, "%u", part->sz);
    argv[9] = partSize;
    // Set multiplier
    argv[10] = mulNames[img->mul];
    // Set partition number
    char partNumS[20];
    (void) snprintf (partNumS, 20, "%d", partNum);
    argv[11] = partNumS;
    argv[12] = getprogname();
    char mulSize[20];
    (void) snprintf (mulSize, 20, "%u", muls[img->mul]);
    argv[13] = mulSize;
    argv[14] = NULL;
    // Run the script
    if (!runScript (scriptDirName, action, argv))
    {
        scriptDirName[dirNameSz] = 0;
        return false;
    }
    scriptDirName[dirNameSz] = 0;
    return true;
}

bool createImages (ListHead_t* images, const char* action, bool overwrite, const char* file, const char* listFile)
{
    // Initialize script dirname
    char* dirName = getenv ("NNSCRIPTROOT");
    if (!dirName)
    {
        error ("variable NNSCRIPTROOT must be set");
        return false;
    }
    scriptDirName = calloc_s (strlen (dirName) + MAX_SCRIPTSZ);
    if (!scriptDirName)
        return false;
    // Copy over dirName
    strcpy (scriptDirName, dirName);
    dirNameSz = strlen (dirName);
    // Get mount directory
    mountDir = getenv ("NNMOUNTDIR");
    if (!mountDir)
    {
        error ("variable NNMOUNTDIR must be set");
        free (scriptDirName);
        return false;
    }
    // Get host prefix
    hostPrefix = getenv ("NNDESTDIR");
    if (!hostPrefix)
    {
        error ("variable NNDESTDIR must be set");
        free (scriptDirName);
        return false;
    }
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
        // Ensure isofloppy boot mode wasn't specified on ISO9660
        if (img->format != IMG_FORMAT_ISO9660 && img->bootMode == IMG_BOOTMODE_ISOFLOPPY)
        {
            error ("boot mode \"isofloppy\" only valid for ISO9660 images");
            goto nextImg;
        }
        if (!createImage (img, action, overwrite, file))
        {
            close (img->fileNo);
            goto nextImg;
        }
        close (img->fileNo);
        // Create partition table
        if (!createPartitionTable (img, action))
            goto nextImg;
        ListEntry_t* partEntry = ListFront (img->partsList);
        while (partEntry)
        {
            ++partNum;
            Partition_t* part = ListEntryData (partEntry);
            // Go through partitions
            if (!strcmp (action, "all") || !strcmp (action, "partition"))
            {
                // Check required fields
                if (!part->prefix)
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
                    free (scriptDirName);
                    return false;
                }
                // Mount the partition
                if (!mountPartition (action, img, part))
                    goto nextPart;
                // Actually do the update
                if (!updatePartition (img, part, listFile, mountDir, hostPrefix))
                {
                    cleanPartition (action, img, part);
                    goto nextPart;
                }
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
        // Cleanup and commit partition data
        imgEntry = ListIterate (imgEntry);
    }
    free (scriptDirName);
    return true;
}

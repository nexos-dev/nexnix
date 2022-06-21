/*
    update.c - handles updating the image's contents
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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nnimage.h"

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

// File entry in list file
typedef struct _lstEntry
{
    const char* destFile;    // File to copy to
    const char* srcFile;     // File to copy from
    bool wildcard;           // If this is a directory that shouls be coped recursively
} listFile_t;

// Gets next file entry
listFile_t* getNextFile()
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
        }
    }
    if (!lineEndFound)
    {
        // That's an error
        error ("no line end detected");
        return (listFile_t*) -1;
    }
    // Check if this pertains to this partition
    if (strstr (buf, curPart->prefix) != buf)
        goto readLoop;
    // Strip that part
    size_t prefixLen = strlen (curPart->prefix);
    char* file = buf + prefixLen;
    if (*file == '/')
        ++file;
    else
    {
        // Ensure that the last character in the prefix is a slash
        // If we don't, then it the prefix is say 'test' and the first component
        // of the path in the file is 'test2' then this will match.
        // By ensuring that the last character of the prefix is a slash
        // when the first character of the stripped path is not
        // will prevent this case
        if (*(curPart->prefix + prefixLen) != '/')
            goto readLoop;
    }
    // Check if last character of the file path is a wildcard ('*')
    if (*(file + strlen (file) - 1) == '*')
    {
        // Take note
        fileEnt->wildcard = true;
        // strip it
        buf[strlen (buf) - 2] = 0;
        // Strip leading to '*'
        if (*file == '*')
            *file = 0;
    }
    // Prepare source buffer
    if (strlcpy (src, hostPrefix, 255) >= 254)
    {
        error ("prefix too large");
        return NULL;
    }
    // Detect if a '/' is at the end
    if (src[strlen (hostPrefix) - 1] != '/')
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

// Updates a file on disk
bool updateFile (const char* src, const char* dest)
{
    // Stat source
    struct stat srcSt;
    if (stat (src, &srcSt) == -1)
    {
        error ("%s: %s", src, strerror (errno));
        return false;
    }
    // Stat dest
    struct stat destSt;
    if (stat (dest, &destSt) == -1 && errno != ENOENT)
    {
        error ("%s:%s", dest, strerror (errno));
        return false;
    }
    // Check if we need to update this file
    if (errno == ENOENT || (srcSt.st_mtim.tv_sec > destSt.st_mtim.tv_sec))
    {
        // Create the file
        int destFd = open (dest, O_WRONLY | O_TRUNC | O_CREAT);
        if (destFd == -1)
        {
            error ("%s: %s", dest, strerror (errno));
            return false;
        }
        // Open source
        int srcFd = open (src, O_RDONLY);
        if (srcFd == -1)
        {
            close (destFd);
            error ("%s: %s", src, strerror (errno));
            return false;
        }
        // Start the copy loop
        off_t blocks = (srcSt.st_size + (srcSt.st_size - 1)) / ((off_t) 1024 * 1024);
        printf ("%lu\n", blocks);
        // Cleanup
        close (destFd);
        close (srcFd);
        return true;
    }
    else
        return true;
}

bool updatePartition (Image_t* img,
                      Partition_t* part,
                      const char* listFile,
                      const char* mount,
                      const char* hostDir)
{
    // Open up list file
    listFileFd = fopen (listFile, "r");
    if (!listFileFd)
    {
        error ("%s: %s", listFile, strerror (errno));
        return false;
    }
    // Set globals
    listFileName = listFile;
    curPart = part;
    mountDir = mount;
    hostPrefix = hostDir;
    listFile_t* curFile = getNextFile();
    while (curFile)
    {
        if (curFile == (listFile_t*) -1)
            return false;
        // Update file
        if (!updateFile (curFile->srcFile, curFile->destFile))
        {
            free (curFile);
            return false;
        }
        free (curFile);
        curFile = getNextFile();
    }
    //  Begin parsing file list
    if (fclose (listFileFd) == EOF)
    {
        error ("%s:%s", listFile, strerror (errno));
        return false;
    }
    return true;
}

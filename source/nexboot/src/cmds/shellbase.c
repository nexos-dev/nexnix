/*
    shellbase.c - contains base commands
    Copyright 2023 - 2024 The NexNix Project

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

#include <libnex/array.h>
#include <nexboot/drivers/display.h>
#include <nexboot/nexboot.h>
#include <nexboot/shell.h>
#include <stdlib.h>
#include <string.h>

// Echo command
bool NbEchoMain (Array_t* args)
{
    // Iterate through each argument and print it
    ArrayIter_t iterS = {0};
    ArrayIter_t* iter = ArrayIterate (args, &iterS);
    while (iter)
    {
        StringRef_t** arg = iter->ptr;
        NbShellWrite ("%s ", StrRefGet (*arg));
        iter = ArrayIterate (args, iter);
    }
    NbShellWrite ("\n");
    return true;
}

// pwd command
bool NbPwdMain (Array_t* args)
{
    StringRef_t* workDir = NbShellGetWorkDir();
    if (!workDir)
        return true;    // Nothing to do
    NbShellWrite ("%s\n", StrRefGet (workDir));
    return true;
}

// cd command
bool NbCdMain (Array_t* args)
{
    // Ensure we have one argument
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("cd: argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** dir = ArrayGetElement (args, 0);
    if (!dir)
    {
        NbShellWrite ("cd: argument required\n");
        return false;
    }
    // Ensure root directory is valid
    NbObject_t* rootFs = NbShellGetRootFs();
    if (!rootFs)
    {
        NbShellWrite ("cd: No valid root directory\n");
        return false;
    }
    // Create full directory, getting current working directory first
    StringRef_t* fullDir = NbShellGetFullPath (StrRefGet (*dir));
    // Ensure directory is valid
    NbFileInfo_t info;
    strcpy (info.name, StrRefGet (fullDir));
    if (!NbVfsGetFileInfo (rootFs, &info))
    {
        NbShellWrite ("cd: Unable to find directory \"%s\"\n", info.name);
        return false;
    }
    // Ensure it is a directory
    if (info.type != NB_FILE_DIR)
    {
        NbShellWrite ("cd: \"%s\" is not a directory\n", info.name);
        return false;
    }
    // Set it
    StringRef_t* name = StrRefCreate ("cwd");
    StrRefNoFree (name);
    NbShellSetVar (name, fullDir);
    StrRefDestroy (fullDir);
    return true;
}

// find command
bool NbFindMain (Array_t* args)
{
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("find: Argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** dir = ArrayGetElement (args, 0);
    if (!dir)
    {
        NbShellWrite ("find: Argument required\n");
        return false;
    }
    // Ensure root directory is valid
    NbObject_t* rootFs = NbShellGetRootFs();
    if (!rootFs)
    {
        NbShellWrite ("find: No valid root directory\n");
        return false;
    }
    // Call get file info
    NbFileInfo_t info = {0};
    if (!NbShellGetFileInfo (rootFs, StrRefGet (*dir), &info))
    {
        NbShellWrite ("find: Unable to get info on file \"%s\"\n", StrRefGet (*dir));
        return false;
    }
    // Print out info
    NbShellWrite ("Found file %s\n", info.name);
    NbShellWrite ("Size: %u\n", info.size);
    NbShellWrite ("Filesystem: %s\n", info.fileSys->name);
    NbShellWrite ("File type: ");
    if (info.type == NB_FILE_FILE)
        NbShellWrite ("regular file\n");
    else if (info.type == NB_FILE_DIR)
        NbShellWrite ("directory\n");
    return true;
}

// Reads a file
bool NbReadMain (Array_t* args)
{
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("read: Argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** fileName = ArrayGetElement (args, 0);
    if (!fileName)
    {
        NbShellWrite ("read: Argument required\n");
        return false;
    }
    // Ensure root directory is valid
    NbObject_t* rootFs = NbShellGetRootFs();
    if (!rootFs)
    {
        NbShellWrite ("read: No valid root directory\n");
        return false;
    }
    // Open up file
    NbFile_t* file = NbShellOpenFile (rootFs, StrRefGet (*fileName));
    if (!file)
    {
        NbShellWrite ("read: Unable to open file \"%s\"\n", StrRefGet (*fileName));
        return false;
    }
    // Begin reading and dumping
    char buf[4096] = {0};
    int32_t bytesRead = NbVfsReadFile (rootFs, file, buf, 4096);
    while (bytesRead)
    {
        NbShellWritePaged (buf);
        memset (buf, 0, 4096);
        bytesRead = NbVfsReadFile (rootFs, file, buf, 4096);
        if (bytesRead == -1)
        {
            NbShellWrite ("read: Error occurred while reading file \"%s\"\n",
                          StrRefGet (*fileName));
            return false;
        }
    }
    NbVfsCloseFile (rootFs, file);
    return true;
}

// ls entry
bool NbLsMain (Array_t* args)
{
    // Get argument
    StringRef_t** dirPtr = ArrayGetElement (args, 0);
    StringRef_t* dir = NULL;
    if (!dirPtr)
    {
        dir = StrRefCreate ("");
        StrRefNoFree (dir);
    }
    else
        dir = *dirPtr;
    // Ensure root directory is valid
    NbObject_t* rootFs = NbShellGetRootFs();
    if (!rootFs)
    {
        NbShellWrite ("ls: No valid root directory\n");
        return false;
    }
    // Open directory
    NbDirIter_t iter = {0};
    StringRef_t* fullDir = NbShellGetFullPath (StrRefGet (dir));
    if (!NbVfsGetDir (rootFs, StrRefGet (fullDir), &iter))
    {
        NbShellWrite ("ls: unable to read directory \"%s\"\n", StrRefGet (dir));
        return false;
    }
    while (iter.name[0])
    {
        NbShellWrite ("%s\n", iter.name);
        if (!NbVfsReadDir (rootFs, &iter))
        {
            NbShellWrite ("ls: unable to read directory \"%s\"\n", StrRefGet (dir));
            return false;
        }
    }
    return true;
}

void NbMmDumpData();

// Dumps memory management information
bool NbMmDumpMain (Array_t* args)
{
    NbMmDumpData();
    return true;
}

void NbMmapDumpData();

bool NbMmapDumpMain (Array_t* args)
{
    NbMmapDumpData();
    return true;
}

// gfxmode command
bool NbGfxModeMain (Array_t* args)
{
    // Grab argument
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("gfxmode: Argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** modeRef = ArrayGetElement (args, 0);
    if (!modeRef)
    {
        NbShellWrite ("gfxmode: Argument required\n");
        return false;
    }
    char* modeStr = StrRefGet (*modeRef);
    // Get individual parts
    char width[5] = {0};
    char height[5] = {0};
    int idx = 0;
    while (*modeStr != 'x')
        width[idx] = *modeStr, ++modeStr, ++idx;
    idx = 0;
    ++modeStr;    // Skip 'x'
    while (*modeStr)
        height[idx] = *modeStr, ++modeStr, ++idx;
    int widthInt = atoi (width);
    int heightInt = atoi (height);
    // Get first display
    NbObject_t* iter = NULL;
    bool found = false;
    NbObject_t* devDir = NbObjFind ("/Devices");
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        if (iter->type == OBJ_TYPE_DEVICE && iter->interface == OBJ_INTERFACE_DISPLAY)
        {
            found = true;
            break;
        }
    }
    if (!iter)
    {
        NbShellWrite ("gfxmode: No display found\n");
        return false;
    }
    // Set mode
    NbDisplayMode_t mode;
    mode.width = widthInt;
    mode.height = heightInt;
    NbObjCallSvc (iter, NB_DISPLAY_SETMODE, &mode);
    return true;
}

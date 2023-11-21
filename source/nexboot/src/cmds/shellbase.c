/*
    shellbase.c - contains base commands
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

#include <libnex/array.h>
#include <nexboot/nexboot.h>
#include <nexboot/shell.h>
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
        NbShellWrite ("read: Unable to open file \"%s\"", StrRefGet (*fileName));
        return false;
    }
    // Begin reading and dumping
    char buf[512] = {0};
    int32_t bytesRead = NbVfsReadFile (rootFs, file, buf, 512);
    do
    {
        NbShellWritePaged (buf);
        bytesRead = NbVfsReadFile (rootFs, file, buf, 512);
        if (bytesRead == -1)
        {
            NbShellWrite ("read: Error occurred while reading file \"%s\"",
                          StrRefGet (*fileName));
            return false;
        }
    } while (bytesRead);
    NbVfsCloseFile (rootFs, file);
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

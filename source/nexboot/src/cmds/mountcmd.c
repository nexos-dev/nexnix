/*
    mountcmd.c - contains mount and unmount command
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
#include <stdio.h>
#include <string.h>

bool NbMountMain (Array_t* args)
{
    // Ensure we have one argument
    if (args->allocatedElems < 2)
    {
        NbShellWrite ("mount: 2 arguments required\n");
        return false;
    }
    // Get arguments
    StringRef_t** volName = ArrayGetElement (args, 0);
    if (!volName)
    {
        NbShellWrite ("mount: 2 arguments required\n");
        return false;
    }
    StringRef_t** mountName = ArrayGetElement (args, 1);
    if (!mountName)
    {
        NbShellWrite ("mount: 2 arguments required\n");
        return false;
    }
    // Get volume
    NbObject_t* volume = NbObjFind (StrRefGet (*volName));
    if (!volume)
    {
        NbShellWrite ("mount: Volume \"%s\" doesn't exist\n", StrRefGet (*volName));
        return false;
    }
    // Call the mount function
    if (!NbVfsMountFs (volume, StrRefGet (*mountName)))
    {
        NbShellWrite ("mount: unable to mount volume \"%s\"\n",
                      StrRefGet (*mountName));
        return false;
    }
    return true;
}

bool NbUnmountMain (Array_t* args)
{
    // Ensure we have one argument
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("unmount: argument required\n");
        return false;
    }
    // Get arguments
    StringRef_t** mountName = ArrayGetElement (args, 0);
    if (!mountName)
    {
        NbShellWrite ("unmount: argument required\n");
        return false;
    }
    // Get filesystem
    char buf[256];
    snprintf (buf, 256, "/Interfaces/FileSys/%s", StrRefGet (*mountName));
    NbObject_t* fs = NbObjFind (buf);
    NbVfsUnmount (fs);
    return true;
}

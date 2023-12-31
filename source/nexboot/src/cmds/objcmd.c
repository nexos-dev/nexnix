/*
    objcmd.c - contains object commands
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

#include <assert.h>
#include <libnex/array.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/shell.h>
#include <string.h>

// Object type table
static const char* objTypeNames[] = {"Object directory",
                                     "Device",
                                     "System info",
                                     "Log",
                                     "Virtual filesystem"};

static const char* objInterfaceNames[] = {"",
                                          "Console",
                                          "Keyboard",
                                          "Generic timer",
                                          "RS232 serial port",
                                          "Terminal",
                                          "Storage device",
                                          "Disk volume"};

// objfind command
bool NbObjFindMain (Array_t* args)
{
    // Ensure we have one argument
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("objfind: argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** objName = ArrayGetElement (args, 0);
    if (!objName)
    {
        NbShellWrite ("objfind: argument required\n");
        return false;
    }
    // Get object from tree
    NbObject_t* obj = NbObjFind (StrRefGet (*objName));
    if (!obj)
    {
        NbShellWrite ("objfind: unable to find object \"%s\"\n", StrRefGet (*objName));
        return true;
    }
    // Print out the info
    NbShellWrite ("Found object %s\n", StrRefGet (*objName));
    assert (NbObjGetType (obj) <= OBJ_MAX_TYPE);
    NbShellWrite ("Object type: %s\n", objTypeNames[NbObjGetType (obj)]);
    assert (NbObjGetInterface (obj) <= OBJ_MAX_INTERFACE);
    if (NbObjGetInterface (obj))
        NbShellWrite ("Object interface: %s\n", objInterfaceNames[NbObjGetInterface (obj)]);
    if (obj->parent)
        NbShellWrite ("Parent directory: %s\n", obj->parent->name);
    if (obj->owner)
        NbShellWrite ("Owner driver: %s\n", obj->owner->name);
    if (obj->manager)
        NbShellWrite ("Manager driver: %s\n", obj->manager->name);
    return true;
}

// lsobj command
bool NbLsObjMain (Array_t* args)
{
    // Ensure we have one argument
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("lsobj: argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** objName = ArrayGetElement (args, 0);
    if (!objName)
    {
        NbShellWrite ("lsobj: argument required\n");
        return false;
    }
    // Get object from tree
    NbObject_t* dir = NbObjFind (StrRefGet (*objName));
    if (!dir)
    {
        NbShellWrite ("lsobj: unable to find object \"%s\"\n", StrRefGet (*objName));
        return true;
    }
    // Iterate through directory
    NbObject_t* iter = NULL;
    while ((iter = NbObjEnumDir (dir, iter)))
    {
        // Print out name and type only
        NbShellWrite ("%s, %s\n", iter->name, objTypeNames[NbObjGetType (iter)]);
    }
    return true;
}

// objdump command
bool NbObjDumpMain (Array_t* args)
{
    // Ensure we have one argument
    if (args->allocatedElems < 1)
    {
        NbShellWrite ("objdump: argument required\n");
        return false;
    }
    // Get argument
    StringRef_t** objName = ArrayGetElement (args, 0);
    if (!objName)
    {
        NbShellWrite ("objdump: argument required\n");
        return false;
    }
    // Get object from tree
    NbObject_t* obj = NbObjFind (StrRefGet (*objName));
    if (!obj)
    {
        NbShellWrite ("objdump: unable to find object \"%s\"\n", StrRefGet (*objName));
        return true;
    }
    // Call dump data service, ensuring it calls NbShellWritePaged
    NbObjCallSvc (obj, OBJ_SERVICE_DUMPDATA, NbShellWritePaged);
    return true;
}

// sysinfo command
bool NbSysinfoMain (Array_t* args)
{
    // Open sysinfo object
    NbObject_t* sysInfoObj = NbObjFind ("/Devices/Sysinfo");
    assert (sysInfoObj);
    NbSysInfo_t* sysInfo = NbObjGetData (sysInfoObj);
    // Dump it
    NbShellWrite ("System name: %s\n", sysInfo->sysType);
    NbShellWrite ("System firmware: ");
    if (sysInfo->sysFwType == NB_FW_TYPE_BIOS)
        NbShellWrite ("bios\n");
    else if (sysInfo->sysFwType == NB_FW_TYPE_EFI)
        NbShellWrite ("efi\n");
    NbShellWrite ("CPU family: ");
    if (sysInfo->cpuInfo.family == NB_CPU_FAMILY_X86)
        NbShellWrite ("x86\n");
    else if (sysInfo->cpuInfo.family == NB_CPU_FAMILY_ARM)
        NbShellWrite ("ARM\n");
    else if (sysInfo->cpuInfo.family == NB_CPU_FAMILY_RISCV)
        NbShellWrite ("RISC-V\n");
    NbShellWrite ("CPU architecture: ");
    if (sysInfo->cpuInfo.arch == NB_CPU_ARCH_I386)
        NbShellWrite ("i386\n");
    else if (sysInfo->cpuInfo.arch == NB_CPU_ARCH_X86_64)
        NbShellWrite ("x86_64\n");
    else if (sysInfo->cpuInfo.arch == NB_CPU_ARCH_ARMV8)
        NbShellWrite ("ARMv8\n");
    else if (sysInfo->cpuInfo.arch == NB_CPU_ARCH_RISCV64)
        NbShellWrite ("RISCV64\n");
    NbShellWrite ("CPU version: ");
    if (sysInfo->cpuInfo.version == NB_CPU_VERSION_386)
        NbShellWrite ("386\n");
    else if (sysInfo->cpuInfo.version == NB_CPU_VERSION_486)
        NbShellWrite ("486\n");
    else if (sysInfo->cpuInfo.version == NB_CPU_VERSION_CPUID)
        NbShellWrite ("486+\n");
    else
        NbShellWrite ("\n");
    NbShellWrite ("CPU flags: ");
    if (sysInfo->cpuInfo.flags & NB_CPU_FLAG_FPU_EXISTS)
        NbShellWrite ("FPU exists ");
    NbShellWrite ("\n");
#ifdef NEXNIX_FW_BIOS
    NbShellWrite ("BIOS boot drive: %#X\n", sysInfo->bootDrive);
#endif
    return true;
}

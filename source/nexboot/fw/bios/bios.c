/*
    bios.c - contains BIOS abstraction layer of nexboot
    Copyright 2022, 2023 The NexNix Project

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

#include <nexboot/drivers/disk.h>
#include <nexboot/fw.h>
#include <nexboot/object.h>
#include <string.h>

// Location of bioscall blob
#define NEXBOOT_BIOSCALL_BLOB 0x1000
#define NEXBOOT_MBRCALL_BLOB  0x2000

// Calls NbBiosCall in binary blob
void NbBiosCall (uint32_t intNo, NbBiosRegs_t* in, NbBiosRegs_t* out)
{
    // Create function pointer for bioscall
    void (*bioscall) (uintptr_t, NbBiosRegs_t*, NbBiosRegs_t*) = (void*) NEXBOOT_BIOSCALL_BLOB;
    bioscall (intNo, in, out);
}

// Calls NbBiosCall
void NbBiosCallMbr (uint8_t driveNum)
{
    void (*mbrcall) (uintptr_t) = (void*) NEXBOOT_MBRCALL_BLOB;
    mbrcall (driveNum);
}

// Print a character to the BIOS screen
void NbFwEarlyPrint (char c)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x0E;
    in.al = (uint8_t) c;
    NbBiosCall (0x10, &in, &out);
}

uintptr_t curMemLocation = NEXBOOT_BIOS_MEMBASE;

uintptr_t NbFwAllocPage()
{
    uintptr_t ret = curMemLocation;
    curMemLocation += NEXBOOT_CPU_PAGE_SIZE;
    if (curMemLocation >= NEXBOOT_BIOS_BASE)
    {
        curMemLocation -= NEXBOOT_CPU_PAGE_SIZE;
        return 0;
    }
    memset ((void*) ret, 0, NEXBOOT_CPU_PAGE_SIZE);
    return ret;
}

uintptr_t NbFwAllocPages (int count)
{
    uintptr_t ret = curMemLocation;
    curMemLocation += (NEXBOOT_CPU_PAGE_SIZE * count);
    if (curMemLocation >= NEXBOOT_BIOS_BASE)
    {
        curMemLocation -= (NEXBOOT_CPU_PAGE_SIZE * count);
        return 0;
    }
    memset ((void*) ret, 0, (NEXBOOT_CPU_PAGE_SIZE * count));
    return ret;
}

// Map in memory regions to address space
void NbFwMapRegions (NbMemEntry_t* memMap, size_t mapSz)
{
    // Unmap VBE memory regions
    // Find VBE display
    bool foundDisplay = false;
    NbObject_t* iter = NULL;
    NbObject_t* devs = NbObjFind ("/Devices");
    while ((iter = NbObjEnumDir (devs, iter)))
    {
        if (iter->type == OBJ_TYPE_DEVICE && iter->interface == OBJ_INTERFACE_DISPLAY)
        {
            foundDisplay = true;
            break;
        }
    }
    if (foundDisplay)
    {
        // Unmap it's buffers
        NbObjCallSvc (iter, NB_VBE_UNMAP_FB, NULL);
    }
}

// Find which disk is the boot disk
NbObject_t* NbFwGetBootDisk()
{
    // Get boot drive
    NbSysInfo_t* sysInfo = NbObjGetData (NbObjFind ("/Devices/Sysinfo"));
    // Iterate through all disk objects
    NbObject_t* iter = NULL;
    NbObject_t* devDir = NbObjFind ("/Devices");
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        if (iter->type == OBJ_TYPE_DEVICE && iter->interface == OBJ_INTERFACE_DISK)
        {
            // Get drive number
            NbDiskInfo_t* diskInf = NbObjGetData (iter);
            NbBiosDisk_t* disk = diskInf->internal;
            if (disk->biosNum == sysInfo->bootDrive)
            {
                // This is it
                return iter;
            }
        }
    }
    return NULL;
}

void NbFwExit()
{
}

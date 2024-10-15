/*
    bios.c - contains BIOS abstraction layer of nexboot
    Copyright 2022 - 2024 The NexNix Project

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
#include <nexboot/nexboot.h>
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
    // Serial port writing is intensely slow on some computers, disable it
    // in.dx = 0;
    // in.al = (uint8_t) c;
    // in.ah = 1;
    // NbBiosCall (0x14, &in, &out);
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

static uintptr_t curOffset = 0;

uintptr_t NbFwAllocPersistentPage()
{
    uintptr_t ret = curOffset + NEXBOOT_BIOS_END;
    curOffset += NEXBOOT_CPU_PAGE_SIZE;
    // Map it
    NbCpuAsMap (ret, ret, NB_CPU_AS_RW);
    return ret;
}

uintptr_t NbFwAllocPersistPageNoMap()
{
    uintptr_t ret = curOffset + NEXBOOT_BIOS_END;
    curOffset += NEXBOOT_CPU_PAGE_SIZE;
    return ret;
}

uintptr_t NbFwAllocPersistentPages (int count)
{
    uintptr_t ret = curOffset + NEXBOOT_BIOS_END;
    curOffset += (count * NEXBOOT_CPU_PAGE_SIZE);
    for (int i = 0; i < count; ++i)
    {
        // Map it
        NbCpuAsMap (ret + (i * NEXBOOT_CPU_PAGE_SIZE),
                    ret + (i * NEXBOOT_CPU_PAGE_SIZE),
                    NB_CPU_AS_RW);
    }
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
    // Reserve bootloader memory regions
    NbFwResvMem (0x0, 0x1000, NEXBOOT_MEM_RESVD);
    // Get EBDA base
    NbBiosRegs_t in = {0}, out = {0};
    NbBiosCall (0x12, &in, &out);
    // Convert to first byte of EBDA
    uint32_t ebdaStart = out.ax * 1024;
    NbFwResvMem (ebdaStart, 0x100000 - ebdaStart, NEXBOOT_MEM_RESVD);
    NbLogMessage ("nexboot: Reserving memory region from %#lX to %#lX as boot reclaim\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  0x100000,
                  NEXBOOT_BIOS_END - 0x100000);
    NbFwResvMem (0x100000, NEXBOOT_BIOS_END - 0x100000, NEXBOOT_MEM_BOOT_RECLAIM);
    NbLogMessage ("nexboot: Reserving memory region from %#lX to %#lX as kernel memory\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  NEXBOOT_BIOS_END,
                  curOffset);
    NbFwResvMem (NEXBOOT_BIOS_END, curOffset, NEXBOOT_MEM_RESVD);
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

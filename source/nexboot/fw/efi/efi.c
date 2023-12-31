/*
    efi.c - contains basic EFI abstractions
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
#include <nexboot/drivers/disk.h>
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <string.h>

// Print a character to ConOut
void NbFwEarlyPrint (char c)
{
    CHAR16 buf[3];
    buf[0] = c;
    buf[1] = 0;
    ST->ConOut->OutputString (ST->ConOut, buf);
}

// Allocate a page of memory
uintptr_t NbFwAllocPage()
{
    EFI_PHYSICAL_ADDRESS addr = 0;
    if (BS->AllocatePages (AllocateAnyPages, EfiLoaderData, 1, &addr) != EFI_SUCCESS)
    {
        return 0;
    }
    memset ((void*) addr, 0, NEXBOOT_CPU_PAGE_SIZE);
    return addr;
}

// Allocates pages of memory
uintptr_t NbFwAllocPages (int count)
{
    EFI_PHYSICAL_ADDRESS addr = 0;
    if (BS->AllocatePages (AllocateAnyPages, EfiLoaderData, count, &addr) != EFI_SUCCESS)
    {
        return 0;
    }
    memset ((void*) addr, 0, count * NEXBOOT_CPU_PAGE_SIZE);
    return addr;
}
// Allocates a page that will persist after bootloader
uintptr_t NbFwAllocPersistentPage()
{
    EFI_PHYSICAL_ADDRESS addr = 0;
    if (BS->AllocatePages (AllocateAnyPages, EfiUnusableMemory, 1, &addr) != EFI_SUCCESS)
    {
        return 0;
    }
    memset ((void*) addr, 0, NEXBOOT_CPU_PAGE_SIZE);
    return addr;
}

// Allocates a page that will persist after bootloader
uintptr_t NbFwAllocPersistentPage()
{
    EFI_PHYSICAL_ADDRESS addr = 0;
    if (BS->AllocatePages (AllocateAnyPages, EfiUnusableMemory, count, &addr) != EFI_SUCCESS)
    {
        return 0;
    }
    memset ((void*) addr, 0, count * NEXBOOT_CPU_PAGE_SIZE);
    return addr;
}

// Allocates pool memory
void* NbEfiAllocPool (size_t sz)
{
    void* buf = NULL;
    if (BS->AllocatePool (EfiLoaderData, sz, &buf) != EFI_SUCCESS)
        return NULL;
    return buf;
}

// Frees pool memory
void NbEfiFreePool (void* buf)
{
    BS->FreePool (buf);
}

// Locates a handle within the system based on it's protocol GUID
EFI_HANDLE* NbEfiLocateHandle (EFI_GUID* protocol, size_t* bufSz)
{
    EFI_HANDLE tmpBuf[1];
    UINTN sz = 0;
    if (BS->LocateHandle (ByProtocol, protocol, NULL, &sz, tmpBuf) != EFI_BUFFER_TOO_SMALL)
        return NULL;
    EFI_HANDLE* handles = NbEfiAllocPool (sz);
    if (BS->LocateHandle (ByProtocol, protocol, NULL, &sz, handles) != EFI_SUCCESS)
    {
        NbEfiFreePool (handles);
        return NULL;
    }
    *bufSz = sz / sizeof (EFI_HANDLE);
    return handles;
}

// Splits a device path by protocol
EFI_HANDLE NbEfiLocateDevicePath (EFI_GUID* protocol, EFI_DEVICE_PATH** devPath)
{
    EFI_HANDLE devHandle = 0;
    if (BS->LocateDevicePath (protocol, devPath, &devHandle) != EFI_SUCCESS)
    {
        return 0;
    }
    return devHandle;
}

// Opens up protocol on specified handle
void* NbEfiOpenProtocol (EFI_HANDLE handle, EFI_GUID* protocol)
{
    void* interface = NULL;
    if (BS->OpenProtocol (handle,
                          protocol,
                          &interface,
                          ImgHandle,
                          0,
                          EFI_OPEN_PROTOCOL_GET_PROTOCOL) != EFI_SUCCESS)
    {
        return NULL;
    }
    return interface;
}

// Closes protocol that previously was opened
bool NbEfiCloseProtocol (EFI_HANDLE handle, EFI_GUID* protocol)
{
    if (BS->CloseProtocol (handle, protocol, ImgHandle, 0) != EFI_SUCCESS)
    {
        return false;
    }
    return true;
}

// Locates first protocol interface that implements specified GUID
void* NbEfiLocateProtocol (EFI_GUID* protocol)
{
    void* interface = NULL;
    if (BS->LocateProtocol (protocol, NULL, &interface) != EFI_SUCCESS)
    {
        return NULL;
    }
    return interface;
}

// Device path functions
static EFI_GUID efiDevGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;

// Grabs device path associated with handle
EFI_DEVICE_PATH* NbEfiGetDevicePath (EFI_HANDLE device)
{
    return NbEfiOpenProtocol (device, &efiDevGuid);
}

// Copies EFI device path so alignment is know
EFI_DEVICE_PATH* NbEfiCopyDev (EFI_DEVICE_PATH* dev)
{
    uint16_t len = dev->Length[0] | (dev->Length[1] << 8);
    EFI_DEVICE_PATH* alignedDev = NbEfiAllocPool (len);
    memcpy (alignedDev, dev, len);
    return alignedDev;
}

// Gets length of a device path structure
uint16_t NbEfiGetDevLen (EFI_DEVICE_PATH* dev)
{
    return dev->Length[0] | (dev->Length[1] << 8);
}

// Gets next device path
EFI_DEVICE_PATH* NbEfiNextDev (EFI_DEVICE_PATH* dev)
{
    void* p = dev;
    p += NbEfiGetDevLen (dev);
    return p;
}

// Returns last component of device path
EFI_DEVICE_PATH* NbEfiGetLastDev (EFI_DEVICE_PATH* dev)
{
    EFI_DEVICE_PATH* curDev = dev;
    EFI_DEVICE_PATH* lastDev = NULL;
    while (curDev->Type != 0x7F && curDev->Type != 0xFF)
    {
        lastDev = curDev;
        curDev = NbEfiNextDev (curDev);
    }
    return lastDev;
}

// Duplicates device path
EFI_DEVICE_PATH* NbEfiDupDevicePath (EFI_DEVICE_PATH* dev)
{
    // Get size of path
    EFI_DEVICE_PATH* curDev = dev;
    int size = 0;
    while (curDev->Type != 0x7F && curDev->Type != 0xFF)
    {
        size += NbEfiGetDevLen (curDev);
        curDev = NbEfiNextDev (curDev);
    }
    // Allocate it
    EFI_DEVICE_PATH* newPath = (EFI_DEVICE_PATH*) NbEfiAllocPool (size);
    if (!newPath)
        return NULL;
    // Copy
    memcpy (newPath, dev, size);
    return newPath;
}

// Compares device paths
bool NbEfiCompareDevicePath (EFI_DEVICE_PATH* dev1, EFI_DEVICE_PATH* dev2)
{
    // Get size of path
    EFI_DEVICE_PATH* curDev = dev1;
    int size = 0;
    while (curDev->Type != 0x7F && curDev->Type != 0xFF)
    {
        size += NbEfiGetDevLen (curDev);
        curDev = NbEfiNextDev (curDev);
    }
    curDev = dev2;
    int size2 = 0;
    while (curDev->Type != 0x7F && curDev->Type != 0xFF)
    {
        size2 += NbEfiGetDevLen (curDev);
        curDev = NbEfiNextDev (curDev);
    }
    // Do a memcmp with the larger size
    return !memcmp (dev1, dev2, (size > size2) ? size : size2);
}

// Map in memory regions to address space
void NbFwMapRegions (NbMemEntry_t* memMap, size_t mapSz)
{
#if defined NEXNIX_ARCH_I386 || defined NEXNIX_ARCH_RISCV64
    // Identity map all boot reclaim
    for (int i = 0; i < mapSz; ++i)
    {
        if (memMap[i].type == NEXBOOT_MEM_BOOT_RECLAIM || memMap[i].type == NEXBOOT_MEM_FW_RECLAIM)
        {
            int numPages = memMap[i].sz / NEXBOOT_CPU_PAGE_SIZE;
            for (int j = 0; j < numPages; ++j)
            {
                NbCpuAsMap (memMap[i].base + (j * NEXBOOT_CPU_PAGE_SIZE),
                            memMap[i].base + (j * NEXBOOT_CPU_PAGE_SIZE),
                            NB_CPU_AS_RW);
            }
        }
    }
#endif
}

// Exits from bootloader
void NbFwExitNexboot()
{
    BS->Exit (ImgHandle, EFI_SUCCESS, 0, NULL);
}

void NbFwExit()
{
    uint32_t mapKey = NbEfiGetMapKey();
    // Exit boot services
    BS->ExitBootServices (ImgHandle, mapKey);
}

EFI_GUID loadedImg = EFI_LOADED_IMAGE_PROTOCOL_GUID;

// Disk structure
typedef struct _efidisk
{
    NbDiskInfo_t disk;              // General info about disk
    EFI_HANDLE diskHandle;          // Disk handle
    EFI_BLOCK_IO_PROTOCOL* prot;    // Disk protocol
    EFI_DEVICE_PATH* device;        // Device path of disk device
    uint32_t mediaId;               // Media ID at detection time
} NbEfiDisk_t;

// Find which disk is the boot disk
NbObject_t* NbFwGetBootDisk()
{
    EFI_LOADED_IMAGE_PROTOCOL* image = NbEfiOpenProtocol (ImgHandle, &loadedImg);
    assert (image);
    // Get boot device path
    EFI_DEVICE_PATH* dev = NbEfiGetDevicePath (image->DeviceHandle);
    // Terminate it at end of hardware part (e.g., at first media part)
    EFI_DEVICE_PATH* bootDisk = NbEfiDupDevicePath (dev);
    EFI_DEVICE_PATH* curDev = bootDisk;
    while (curDev->Type != 4)
        curDev = NbEfiNextDev (curDev);
    // Change it so it terminates
    curDev->Type = 0x7F;
    curDev->SubType = 0xFF;
    // And now we just loop through every disk and compare
    NbObject_t* iter = NULL;
    NbObject_t* devDir = NbObjFind ("/Devices");
    while ((iter = NbObjEnumDir (devDir, iter)))
    {
        if (iter->type == OBJ_TYPE_DEVICE && iter->interface == OBJ_INTERFACE_DISK)
        {
            // Get drive number
            NbEfiDisk_t* diskInf = NbObjGetData (iter);
            if (NbEfiCompareDevicePath (diskInf->device, bootDisk))
                return iter;
        }
    }
    return NULL;
}

bool NbOsBootChainload()
{
}

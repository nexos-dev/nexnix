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

#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/object.h>
#include <string.h>

// Print a character to ConOut
void NbFwEarlyPrint (char c)
{
    // Prepare a dummy buffer
    CHAR16 buf[2];
    buf[0] = c;
    buf[1] = 0;
    uefi_call_wrapper (ST->ConOut->OutputString, 2, ST->ConOut, buf);
}

// Allocate a page of memory
uintptr_t NbFwAllocPage()
{
    EFI_PHYSICAL_ADDRESS addr = 0;
    if (uefi_call_wrapper (BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, 1, &addr) !=
        EFI_SUCCESS)
    {
        return 0;
    }
    return addr;
}

// Allocates pages of memory
uintptr_t NbFwAllocPages (int count)
{
    EFI_PHYSICAL_ADDRESS addr = 0;
    if (uefi_call_wrapper (BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, count, &addr) !=
        EFI_SUCCESS)
    {
        return 0;
    }
    return addr;
}

// Allocates pool memory
void* NbEfiAllocPool (size_t sz)
{
    void* buf = NULL;
    if (uefi_call_wrapper (BS->AllocatePool, 3, EfiLoaderData, sz, &buf) != EFI_SUCCESS)
        return NULL;
    return buf;
}

// Frees pool memory
void NbEfiFreePool (void* buf)
{
    uefi_call_wrapper (BS->FreePool, 1, buf);
}

// Locates a handle within the system based on it's protocol GUID
EFI_HANDLE* NbEfiLocateHandle (EFI_GUID* protocol, size_t* bufSz)
{
    EFI_HANDLE* buf = NULL;
    if (uefi_call_wrapper (BS->LocateHandleBuffer, 5, ByProtocol, protocol, NULL, bufSz, &buf) !=
        EFI_SUCCESS)
    {
        return NULL;
    }
    return buf;
}

// Splits a device path by protocol
EFI_HANDLE NbEfiLocateDevicePath (EFI_GUID* protocol, EFI_DEVICE_PATH** devPath)
{
    EFI_HANDLE devHandle = 0;
    if (uefi_call_wrapper (BS->LocateDevicePath, 3, protocol, devPath, &devHandle) != EFI_SUCCESS)
    {
        return 0;
    }
    return devHandle;
}

// Opens up protocol on specified handle
void* NbEfiOpenProtocol (EFI_HANDLE handle, EFI_GUID* protocol)
{
    void* interface = NULL;
    if (uefi_call_wrapper (BS->OpenProtocol,
                           6,
                           handle,
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
    if (uefi_call_wrapper (BS->CloseProtocol, 4, handle, protocol, ImgHandle, 0) != EFI_SUCCESS)
    {
        return false;
    }
    return true;
}

// Locates first protocol interface that implements specified GUID
void* NbEfiLocateProtocol (EFI_GUID* protocol)
{
    void* interface = NULL;
    if (uefi_call_wrapper (BS->LocateProtocol, 3, protocol, NULL, &interface) != EFI_SUCCESS)
    {
        return NULL;
    }
    return interface;
}

// Map in memory regions to address space
void NbFwMapRegions()
{
}

// Exits from bootloader
void NbFwExitNexboot()
{
    uefi_call_wrapper (BS->Exit, 4, ImgHandle, EFI_SUCCESS, 0, NULL);
}

// Find which disk is the boot disk
NbObject_t* NbFwGetBootDisk()
{
    return NULL;
}

bool NbOsBootChainload()
{
}

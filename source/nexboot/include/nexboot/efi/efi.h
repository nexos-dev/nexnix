/*
    efi.h - contains EFI functions for nexboot
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

#ifndef _NBEFI_H
#define _NBEFI_H

#include <nexboot/efi/inc/efi.h>
#include <stdbool.h>
#include <stdint.h>

// EFI globals
extern EFI_SYSTEM_TABLE* ST;
extern EFI_BOOT_SERVICES* BS;
extern EFI_RUNTIME_SERVICES* RT;
extern EFI_HANDLE ImgHandle;

/// Prints a character to screen using ConOut
void NbFwEarlyPrint (char c);

// Converts a UTF-16 string to UTF-8 string
char* NbFwConvString (const CHAR16* in);

// Allocates pool memory
void* NbEfiAllocPool (size_t sz);

// Frees pool memory
void NbEfiFreePool (void* buf);

// Locates a handle within the system based on it's protocol GUID
EFI_HANDLE* NbEfiLocateHandle (EFI_GUID* protocol, size_t* bufSz);

// Splits a device path by protocol
EFI_HANDLE NbEfiLocateDevicePath (EFI_GUID* protocol, EFI_DEVICE_PATH** devPath);

// Opens up protocol on specified handle
void* NbEfiOpenProtocol (EFI_HANDLE handle, EFI_GUID* protocol);

// Closes protocol that previously was opened
bool NbEfiCloseProtocol (EFI_HANDLE handle, EFI_GUID* protocol);

// Locates first protocol interface that implements specified GUID
void* NbEfiLocateProtocol (EFI_GUID* protocol);

// Exits from bootloader
void NbFwExitNexboot();

// Gets map key with current memory map
uint32_t NbEfiGetMapKey();

// EFI configuration tables
#define NB_ARCH_COMP_ACPI    0
#define NB_ARCH_COMP_MPS     1
#define NB_ARCH_COMP_PNP     2
#define NB_ARCH_COMP_APM     3
#define NB_ARCH_COMP_SMBIOS  4
#define NB_ARCH_COMP_SMBIOS3 5

#endif

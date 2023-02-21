/*
    efimain.c - contains entry point to NexNix EFI application
    Copyright 2022, 2023 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the Lice0nse for the specific language governing permissions and
    limitations under the License.
*/

#include <nexboot/detect.h>
#include <nexboot/efi/inc/efi.h>
#include <string.h>

// Configuration table GUIDs
// clang-format off
EFI_GUID acpi20Guid = 
        {0x8868e871,0xe4f1,0x11d3,
        {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};

EFI_GUID acpi10Guid =
        {0xeb9d2d30,0x2d88,0x11d3,
        {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};

EFI_GUID smbiosGuid =
        {0xeb9d2d31,0x2d88,0x11d3,
        {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};

EFI_GUID smbios3Guid =
        {0xf2fd1544, 0x9794, 0x4a2c,
        {0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
// clang-format on

// Global EFI variables
EFI_SYSTEM_TABLE* ST = NULL;
EFI_BOOT_SERVICES* BS = NULL;
EFI_RUNTIME_SERVICES* RT = NULL;
EFI_HANDLE ImgHandle;

NbloadDetect_t detect = {0};    // Nbdetect structure

// Prepares nbdetect structure
void nbPrepareNbdetect()
{
    // Prepare fields
    detect.sig = NBLOAD_SIGNATURE;
#ifdef NEXNIX_ARCH_I386
    detect.cpu.arch = NBLOAD_CPU_ARCH_I386;
    detect.cpu.family = NBLOAD_CPU_FAMILY_X86;
    detect.cpu.version = NBLOAD_CPU_VERSION_CPUID;
    detect.cpu.flags = NBLOAD_CPU_FLAG_FPU_EXISTS;
#elif defined(NEXNIX_ARCH_X86_64)
    detect.cpu.arch = NBLOAD_CPU_ARCH_X86_64;
    detect.cpu.family = NBLOAD_CPU_FAMILY_X86;
    detect.cpu.version = NBLOAD_CPU_VERSION_CPUID;
    detect.cpu.flags = NBLOAD_CPU_FLAG_FPU_EXISTS;
#endif
    // Setup system tables by reading EFI system table
    for (int i = 0; i < ST->NumberOfTableEntries; ++i)
    {
        // Check if we recognize this table GUID
        EFI_CONFIGURATION_TABLE* tab = &ST->ConfigurationTable[i];
        if (!memcmp (&tab->VendorGuid, &acpi20Guid, sizeof (EFI_GUID)) ||
            !memcmp (&tab->VendorGuid, &acpi10Guid, sizeof (EFI_GUID)))
        {
            ST->ConOut->OutputString (ST->ConOut, L"Test string 1\r\n");
            // Add to detect structure
            detect.sysTabs.detected |= NBLOAD_TABLE_ACPI;
            detect.sysTabs.tabs[NBLOAD_TABLE_ACPI] = (uintptr_t) tab->VendorTable;
        }
        else if (!memcmp (&tab->VendorGuid, &smbiosGuid, sizeof (EFI_GUID)))
        {
            ST->ConOut->OutputString (ST->ConOut, L"Test string 2\r\n");
            // Add to detect structure
            detect.sysTabs.detected |= NBLOAD_TABLE_SMBIOS;
            detect.sysTabs.tabs[NBLOAD_TABLE_SMBIOS] = (uintptr_t) tab->VendorTable;
        }
        else if (!memcmp (&tab->VendorGuid, &smbios3Guid, sizeof (EFI_GUID)))
        {
            ST->ConOut->OutputString (ST->ConOut, L"Test string 3\r\n");
            // Add to detect structure
            detect.sysTabs.detected |= NBLOAD_TABLE_SMBIOS3;
            detect.sysTabs.tabs[NBLOAD_TABLE_SMBIOS3] = (uintptr_t) tab->VendorTable;
        }
        // Ignore other tables
    }
}

// Entry point into NexNix
EFI_STATUS EFIAPI NbEfiEntry (EFI_HANDLE imgHandle, EFI_SYSTEM_TABLE* efiSysTab)
{
    // Set global variables
    ST = efiSysTab;
    BS = efiSysTab->BootServices;
    RT = efiSysTab->RuntimeServices;
    ImgHandle = imgHandle;
    // Prepare nbdetect structure
    nbPrepareNbdetect();
    return EFI_SUCCESS;
}

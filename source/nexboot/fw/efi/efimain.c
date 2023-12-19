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
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <string.h>

// Global EFI variables
EFI_SYSTEM_TABLE* ST = NULL;
EFI_BOOT_SERVICES* BS = NULL;
EFI_RUNTIME_SERVICES* RT = NULL;
EFI_HANDLE ImgHandle;

static NbloadDetect_t detect = {0};    // Nbdetect structure

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
}

void NbMain (NbloadDetect_t*);

// Entry point into NexNix
EFI_STATUS __attribute__ ((ms_abi)) NbEfiEntry (EFI_HANDLE imgHandle, EFI_SYSTEM_TABLE* efiSysTab)
{
    // Set global variables
    ST = efiSysTab;
    BS = efiSysTab->BootServices;
    RT = efiSysTab->RuntimeServices;
    ImgHandle = imgHandle;
    // Prepare nbdetect structure
    nbPrepareNbdetect();
    // Disarm watchdog
    uefi_call_wrapper (BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    // Call NbMain
    NbMain (&detect);
    return EFI_SUCCESS;
}

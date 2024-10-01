/*
    hwdetect.c - contains EFI hardware detection
    Copyright 2023 - 2024 The NexNix Project

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
#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <stdio.h>
#include <string.h>

// Sysinfo data
static NbSysInfo_t sysInfo = {0};

// Configuration table GUIDs
// clang-format off
EFI_GUID acpi20Guid = ACPI_20_TABLE_GUID;
EFI_GUID mpsGuid = MPS_TABLE_GUID;
EFI_GUID acpi10Guid = ACPI_TABLE_GUID;
EFI_GUID smbiosGuid = SMBIOS_TABLE_GUID;
EFI_GUID smbios3Guid = SMBIOS3_TABLE_GUID;
// clang-format on

// Looks for an EFI configuration table
static void* detectConfTable (EFI_GUID* guid)
{
    // Search table
    for (int i = 0; i < ST->NumberOfTableEntries; ++i)
    {
        if (!memcmp (&ST->ConfigurationTable[i].VendorGuid, guid, sizeof (EFI_GUID)))
            return ST->ConfigurationTable[i].VendorTable;
    }
    return NULL;
}

// Creates a device object
static bool createDeviceObject (const char* name, int interface, NbDriver_t* drv, NbHwDevice_t* dev)
{
    NbObject_t* obj = NbObjCreate (name, OBJ_TYPE_DEVICE, interface);
    NbObjSetData (obj, dev);
    if (!obj)
        return false;
    NbSendDriverCode (drv, NB_DRIVER_ENTRY_ATTACHOBJ, obj);
}

bool NbFwDetectHw (NbloadDetect_t* nbDetect)
{
    strcpy (sysInfo.sysType, "EFI-firmware based system");
    sysInfo.cpuInfo.arch = nbDetect->cpu.arch;
    sysInfo.cpuInfo.family = nbDetect->cpu.family;
    sysInfo.cpuInfo.flags = nbDetect->cpu.flags;
    sysInfo.cpuInfo.version = nbDetect->cpu.version;
    sysInfo.sysFwType = NB_FW_TYPE_EFI;
    // Find EFI configuration tables we need
    void* acpiTab = detectConfTable (&acpi20Guid);
    if (acpiTab)
    {
        sysInfo.comps[NB_ARCH_COMP_ACPI] = (uintptr_t) acpiTab;
        sysInfo.detectedComps |= (1 << NB_ARCH_COMP_ACPI);
    }
    else
    {
        // Look for ACPI 1.0
        acpiTab = detectConfTable (&acpi10Guid);
        if (acpiTab)
        {
            sysInfo.comps[NB_ARCH_COMP_ACPI] = (uintptr_t) acpiTab;
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_ACPI);
        }
    }
    // Find MPS
    void* mpsTab = detectConfTable (&mpsGuid);
    if (mpsTab)
    {
        sysInfo.comps[NB_ARCH_COMP_MPS] = (uintptr_t) mpsTab;
        sysInfo.detectedComps |= (1 << NB_ARCH_COMP_MPS);
    }
    // Find SMBios
    void* smbiosTab = detectConfTable (&smbiosGuid);
    if (smbiosTab)
    {
        sysInfo.comps[NB_ARCH_COMP_SMBIOS] = (uintptr_t) smbiosTab;
        sysInfo.detectedComps |= (1 << NB_ARCH_COMP_SMBIOS);
    }
    // Find SMBios3
    void* smbios3Tab = detectConfTable (&smbios3Guid);
    if (smbios3Tab)
    {
        sysInfo.comps[NB_ARCH_COMP_SMBIOS3] = (uintptr_t) smbios3Tab;
        sysInfo.detectedComps |= (1 << NB_ARCH_COMP_SMBIOS3);
    }
    // Create object
    NbObject_t* sysInfoObj = NbObjCreate ("/Devices/Sysinfo", OBJ_TYPE_SYSINFO, 0);
    NbObjSetData (sysInfoObj, &sysInfo);
    // Search for devices
    // Find serial ports
    NbDriver_t* serialDrv = NbFindDriver ("Rs232_Efi");
    assert (serialDrv);
    NbHwDevice_t* dev = (NbHwDevice_t*) malloc (serialDrv->devSize);
    while (NbSendDriverCode (serialDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        // Create object
        char nameBuf[64] = {0};
        snprintf (nameBuf, 64, "/Devices/EfiSerial%d", dev->devId);
        createDeviceObject (nameBuf, OBJ_INTERFACE_RS232, serialDrv, dev);
        dev = (NbHwDevice_t*) malloc (serialDrv->devSize);
    }
    free (dev);
    // Find keyboards
    NbDriver_t* keyDrv = NbFindDriver ("EfiKbd");
    assert (keyDrv);
    dev = (NbHwDevice_t*) malloc (keyDrv->devSize);
    while (NbSendDriverCode (keyDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        // Create object
        char nameBuf[64] = {0};
        snprintf (nameBuf, 64, "/Devices/EfiKbd%d", dev->devId);
        createDeviceObject (nameBuf, OBJ_INTERFACE_KBD, keyDrv, dev);
        dev = (NbHwDevice_t*) malloc (keyDrv->devSize);
    }
    free (dev);
    // Find disks
    NbDriver_t* diskDrv = NbFindDriver ("EfiDisk");
    assert (diskDrv);
    dev = (NbHwDevice_t*) malloc (diskDrv->devSize);
    while (NbSendDriverCode (diskDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        // Create object
        char nameBuf[64] = {0};
        snprintf (nameBuf, 64, "/Devices/EfiDisk%d", dev->devId);
        createDeviceObject (nameBuf, OBJ_INTERFACE_DISK, diskDrv, dev);
        dev = (NbHwDevice_t*) malloc (diskDrv->devSize);
    }
    free (dev);
    // Find displays
    NbDriver_t* gopDrv = NbFindDriver ("EfiGopFb");
    assert (gopDrv);
    dev = (NbHwDevice_t*) malloc (gopDrv->devSize);
    while (NbSendDriverCode (gopDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        // Create object
        char nameBuf[64] = {0};
        snprintf (nameBuf, 64, "/Devices/GopDisplay%d", dev->devId);
        createDeviceObject (nameBuf, OBJ_INTERFACE_DISPLAY, gopDrv, dev);
        dev = (NbHwDevice_t*) malloc (gopDrv->devSize);
    }
    free (dev);
    return true;
}

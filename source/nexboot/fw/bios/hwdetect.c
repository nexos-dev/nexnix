/*
    hwdetect.c - detects system hardware
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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdio.h>
#include <string.h>

typedef struct _rsdp
{
    uint8_t sig[8];
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t resvd;
    uint32_t rsdtAddr;
} __attribute__ ((packed)) acpiRsdp_t;

// TODO: currently we assume all BIOS system are PCs. Although 99% are,
// there is still stuff like the PC98 and XBox which is BIOS but not PC
// We should seperate PC related stuff into its own layer

static NbSysInfo_t sysInfo = {0};

static void detectAcpi()
{
    // Find RSDP
    uint16_t* bdaEdba = (uint16_t*) 0x40E;
    uintptr_t ebdaBase = (uintptr_t) (*bdaEdba);
    ebdaBase <<= 4;
    // Check EBDA
    for (int i = 0; i < 1024; i += 16)
    {
        if (!memcmp ((const void*) ebdaBase, "RSD PTR ", 8))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_ACPI);
            sysInfo.comps[NB_ARCH_COMP_ACPI] = ebdaBase;
            return;
        }
        ebdaBase += 16;
    }
    // Check ROM
    uintptr_t romBase = 0xE0000;
    for (int i = 0; i < 0x20000; i += 16)
    {
        if (!memcmp ((const void*) romBase, "RSD PTR ", 8))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_ACPI);
            sysInfo.comps[NB_ARCH_COMP_ACPI] = romBase;
            return;
        }
        romBase += 16;
    }
}

static void detectMps()
{
    // Find MP floating table
    uint16_t* bdaEdba = (uint16_t*) 0x40E;
    uintptr_t ebdaBase = (uintptr_t) (*bdaEdba);
    ebdaBase <<= 4;
    // Check EBDA
    for (int i = 0; i < 1024; i += 16)
    {
        if (!memcmp ((const void*) ebdaBase, "_MP_", 4))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_MPS);
            sysInfo.comps[NB_ARCH_COMP_MPS] = ebdaBase;
            return;
        }
        ebdaBase += 4;
    }
    // Check ROM
    uintptr_t romBase = 0xE0000;
    for (int i = 0; i < 0x20000; i += 4)
    {
        if (!memcmp ((const void*) romBase, "_MP_", 4))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_MPS);
            sysInfo.comps[NB_ARCH_COMP_MPS] = romBase;
            return;
        }
        romBase += 4;
    }
}

static void detectPnp()
{
    // Check ROM for PNP installation check struct
    uintptr_t romBase = 0xF0000;
    for (int i = 0; i < 0x10000; i += 4)
    {
        if (!memcmp ((const void*) romBase, "$PnP", 4))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_PNP);
            sysInfo.comps[NB_ARCH_COMP_PNP] = romBase;
            return;
        }
        romBase += 4;
    }
}

static void detectApm()
{
    // Prepare to call APM BIOS
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x53;    // APM function
    in.al = 0;       // APM installation check
    in.bx = 0;
    NbBiosCall (0x15, &in, &out);
    // Check if APM was found
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
        return;
    if (out.ah == 0x86)
        return;
    if (out.bh != 80)
        return;
    if (out.bl != 77)
        return;
    sysInfo.detectedComps |= (1 << NB_ARCH_COMP_APM);
}

static void detectSmbios()
{
    // Check ROM for SMBIOS2 installation check struct
    uintptr_t romBase = 0xF0000;
    for (int i = 0; i < 0x10000; i += 16)
    {
        if (!memcmp ((const void*) romBase, "_SM_", 4))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_SMBIOS);
            sysInfo.comps[NB_ARCH_COMP_SMBIOS] = romBase;
            break;
        }
        romBase += 16;
    }
    // Find SMBIOS3
    romBase = 0xF0000;
    for (int i = 0; i < 0x10000; i += 16)
    {
        if (!memcmp ((const void*) romBase, "_SM3_", 5))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_SMBIOS3);
            sysInfo.comps[NB_ARCH_COMP_SMBIOS3] = romBase;
            break;
        }
        romBase += 16;
    }
}

static void detectPciBios()
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0xB1;
    in.al = 0x01;
    NbBiosCall (0x1A, &in, &out);
    if (out.flags & NEXBOOT_CPU_CARRY_FLAG)
        return;
    if (out.ah)
        return;
    if (out.edx == 0x736780)
        return;
    sysInfo.detectedComps |= (1 << NB_ARCH_COMP_PCI);
}

static void detectVesaBios()
{
    // Run 00h
    NbBiosRegs_t in = {0}, out = {0};
    in.ax = 0x4F00;
    in.es = 0;
    in.di = NEXBOOT_BIOSBUF_BASE;
    uint8_t* vbeSig = (uint8_t*) NEXBOOT_BIOSBUF_BASE;
    vbeSig[0] = 'V';
    vbeSig[1] = 'B';
    vbeSig[2] = 'E';
    vbeSig[3] = '2';
    NbBiosCall (0x10, &in, &out);
    if (out.al != 0x4F || out.ah)
        return;
    if (memcmp (vbeSig, "VESA", 4) != 0)
        return;
    sysInfo.detectedComps |= (1 << NB_ARCH_COMP_VESA);
}

static void detectBios32()
{
    // Find BIOS32
    uintptr_t romBase = 0xE0000;
    for (int i = 0; i < 0x20000; i += 16)
    {
        if (!memcmp ((const void*) romBase, "_32_", 4))
        {
            sysInfo.detectedComps |= (1 << NB_ARCH_COMP_BIOS32);
            sysInfo.comps[NB_ARCH_COMP_BIOS32] = romBase;
            break;
        }
        romBase += 16;
    }
}

// Creates a device object
static bool createDeviceObject (const char* name, int interface, NbDriver_t* drv, NbHwDevice_t* dev)
{
    NbObject_t* obj = NbObjCreate (name, OBJ_TYPE_DEVICE, interface);
    NbObjSetData (obj, dev);
    if (!obj)
        return false;
    return NbSendDriverCode (drv, NB_DRIVER_ENTRY_ATTACHOBJ, obj);
}

// Detects system hardware
bool NbFwDetectHw (NbloadDetect_t* nbDetect)
{
    assert (nbDetect);
    // Set string in sysInfo
    strcpy (sysInfo.sysType, "PC-AT compatible system");
    sysInfo.cpuInfo.arch = nbDetect->cpu.arch;
    sysInfo.cpuInfo.family = nbDetect->cpu.family;
    sysInfo.cpuInfo.flags = nbDetect->cpu.flags;
    sysInfo.cpuInfo.version = nbDetect->cpu.version;
    sysInfo.sysFwType = NB_FW_TYPE_BIOS;
    sysInfo.bootDrive = nbDetect->bootDrive;
    // Create sysinfo object
    NbObject_t* sysInfoObj = NbObjCreate ("/Devices/Sysinfo", OBJ_TYPE_SYSINFO, 0);
    NbObjSetData (sysInfoObj, &sysInfo);
    // Detect various tables
    detectAcpi();
    detectMps();
    detectPnp();
    detectApm();
    detectSmbios();
    detectPciBios();
    detectVesaBios();
    detectBios32();
    // Detect ISA devices the bootloader needs
    // Find keyboards
    NbDriver_t* keyDrv = NbFindDriver ("BiosKbd");
    assert (keyDrv);
    NbHwDevice_t* dev = (NbHwDevice_t*) malloc (keyDrv->devSize);
    while (NbSendDriverCode (keyDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        // Create object for driver
        char buf[64] = {0};
        snprintf (buf, 64, "/Devices/BiosKbd%d", dev->devId);
        createDeviceObject (buf, OBJ_INTERFACE_KBD, keyDrv, dev);
        dev = (NbHwDevice_t*) malloc (keyDrv->devSize);
    }
    free (dev);
    // Find serial ports
    NbDriver_t* serialDrv = NbFindDriver ("Rs232_16550");
    assert (serialDrv);
    dev = (NbHwDevice_t*) malloc (serialDrv->devSize);
    while (NbSendDriverCode (serialDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        // Create object for driver
        char buf[64] = {0};
        snprintf (buf, 64, "/Devices/Rs232%d", dev->devId);
        createDeviceObject (buf, OBJ_INTERFACE_RS232, serialDrv, dev);
        dev = (NbHwDevice_t*) malloc (serialDrv->devSize);
    }
    free (dev);
    // Detect hard disks
    NbDriver_t* biosDiskDrv = NbFindDriver ("BiosDisk");
    assert (biosDiskDrv);
    dev = (NbHwDevice_t*) malloc (biosDiskDrv->devSize);
    while (NbSendDriverCode (biosDiskDrv, NB_DRIVER_ENTRY_DETECTHW, dev))
    {
        char buf[64] = {0};
        snprintf (buf, 64, "/Devices/BiosDisk%d", dev->devId);
        createDeviceObject (buf, OBJ_INTERFACE_DISK, biosDiskDrv, dev);
        dev = (NbHwDevice_t*) malloc (biosDiskDrv->devSize);
    }
    free (dev);
    // Check if we can use VBE
    NbDriver_t* vbeDrv = NbFindDriver ("VbeFb");
    assert (vbeDrv);
    dev = (NbHwDevice_t*) malloc (vbeDrv->devSize);
    // Look for it
    bool res = NbSendDriverCode (vbeDrv, NB_DRIVER_ENTRY_DETECTHW, dev);
    if (!res)
    {
        // No VBE, use VGA console
        free (dev);
        // Find VGA console driver
        NbDriver_t* vgaDrv = NbFindDriver ("VgaConsole");
        assert (vgaDrv);
        dev = (NbHwDevice_t*) malloc (vgaDrv->devSize);
        assert (NbSendDriverCode (vgaDrv, NB_DRIVER_ENTRY_DETECTHW, dev));
        // Attach device to driver
        createDeviceObject ("/Devices/VgaConsole0", OBJ_INTERFACE_CONSOLE, vgaDrv, dev);
    }
    else
    {
        createDeviceObject ("/Devices/VbeDisplay0", OBJ_INTERFACE_DISPLAY, vbeDrv, dev);
    }
    return true;
}

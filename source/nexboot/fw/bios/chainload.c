/*
    chainload.c - contains chainload boot type
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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/os.h>
#include <nexboot/shell.h>
#include <string.h>

bool NbOsBootChainload (NbOsInfo_t* os)
{
    assert (os->payload);
    // Find payload's object
    NbObject_t* bootDev = NbObjFind (StrRefGet (os->payload));
    if (!bootDev)
    {
        NbShellWrite ("boot: payload \"%s\" doesn't exist\n", StrRefGet (os->payload));
        return false;
    }
    // Ensure it's a disk or volume
    if (bootDev->type != OBJ_TYPE_DEVICE ||
        (bootDev->interface != OBJ_INTERFACE_DISK && bootDev->interface != OBJ_INTERFACE_VOLUME))
    {
        NbShellWrite ("boot: payload \"%s\" not disk or volume\n", StrRefGet (os->payload));
        return false;
    }
    // Determine object function to call
    int func = 0;
    if (bootDev->interface == OBJ_INTERFACE_DISK)
        func = NB_DISK_READ_SECTORS;
    else if (bootDev->interface == OBJ_INTERFACE_VOLUME)
        func = NB_VOLUME_READ_SECTORS;
    // Load up sector
    NbReadSector_t sect;
    sect.buf = (void*) NEXBOOT_BIOS_MBR_BASE;
    sect.count = 1;
    sect.sector = 0;
    if (!NbObjCallSvc (bootDev, func, &sect))
    {
        NbShellWrite ("boot: unable to read from device \"%s\"", StrRefGet (os->payload));
        return false;
    }
    // Determine drive number
    int driveNum = 0;
    if (bootDev->interface == OBJ_INTERFACE_DISK)
    {
        NbDiskInfo_t* disk = NbObjGetData (bootDev);
        NbBiosDisk_t* biosDisk = disk->internal;
        driveNum = biosDisk->biosNum;
    }
    if (bootDev->interface == OBJ_INTERFACE_VOLUME)
    {
        NbVolume_t* vol = NbObjGetData (bootDev);
        NbDiskInfo_t* disk = NbObjGetData (vol->disk);
        NbBiosDisk_t* biosDisk = disk->internal;
        driveNum = biosDisk->biosNum;
    }
    // Switch to text mode
    NbBiosRegs_t in = {0}, out = {0};
    in.al = 0x3;
    in.ah = 0x0;
    NbBiosCall (0x10, &in, &out);
    // Jump down to real mode and execute
    NbBiosCallMbr (driveNum);
    return false;
}

/*
    efiserial.c - contains EFI serial port driver
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

#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>

extern NbObjSvcTab_t efiSerialSvcTab;
extern NbDriver_t efiSerialDrv;

static EFI_HANDLE* serialDevs = NULL;
static size_t numDevs = 0;
static int curDev = 0;

static EFI_GUID serialGuid = EFI_SERIAL_IO_PROTOCOL_GUID;

// Device structure
typedef struct _efiserial
{
    NbHwDevice_t dev;
    EFI_HANDLE handle;
    EFI_SERIAL_IO_PROTOCOL* prot;    // Protocol attached to this device
} NbEfiSerialDev_t;

static bool EfiSerialEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {
            // Locate handles if needed
            if (!serialDevs)
            {
                serialDevs = NbEfiLocateHandle (&serialGuid, &numDevs);
                // Ensure it worked
                if (!serialDevs)
                {
                    NbLogMessage ("nbefiserial: no serial ports found\r\n", NEXBOOT_LOGLEVEL_INFO);
                    return false;    // No serial ports
                }
            }
            // Checl if we reached end
            if (curDev == numDevs)
            {
                NbEfiFreePool (serialDevs);
                return false;
            }
            // Prepare devices
            NbEfiSerialDev_t* dev = params;
            dev->dev.devId = curDev;
            dev->dev.sz = sizeof (NbEfiSerialDev_t);
            dev->handle = serialDevs[curDev];
            dev->prot = NbEfiOpenProtocol (dev->handle, &serialGuid);
            if (!dev->prot)
            {
                // Error
                NbEfiFreePool (serialDevs);
                NbLogMessage ("nbefiserial: unable to open EFI serial protocol on port COM%d\r\n",
                              NEXBOOT_LOGLEVEL_ERROR,
                              curDev);
                return false;
            }
            // Reset it
            uefi_call_wrapper (dev->prot->Reset, 1, dev->prot);
            // That's it
            NbLogMessage ("nbefiserial: found EFI serial port COM%d\r\n",
                          NEXBOOT_LOGLEVEL_INFO,
                          curDev);
            ++curDev;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &efiSerialSvcTab);
            NbObjSetManager (obj, &efiSerialDrv);
            break;
        }
    }
    return true;
}

static bool EfiSerialDumpData (void* objp, void* params)
{
    return true;
}

static bool EfiSerialNotify (void* objp, void* params)
{
    // Get notification code
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = params;
    int code = notify->code;
    if (code == NB_SERIAL_NOTIFY_SETOWNER)
    {
        // Notify current owner that we are being deteached
        NbEfiSerialDev_t* console = obj->data;
        if (obj->owner)
            obj->owner->entry (NB_DRIVER_ENTRY_DETACHOBJ, obj);
        NbDriver_t* newDrv = notify->data;
        // Set new owner
        NbObjSetOwner (obj, newDrv);
    }
    return true;
}

static bool EfiSerialWrite (void* objp, void* params)
{
    NbObject_t* serial = objp;
    NbEfiSerialDev_t* dev = NbObjGetData (serial);
    uint8_t c = (uint8_t) params;
    size_t out = 1;
    // Write it out
    if (uefi_call_wrapper (dev->prot->Write, 3, dev->prot, &out, &c) != EFI_SUCCESS)
        return false;
    return true;
}

static bool EfiSerialRead (void* objp, void* params)
{
    NbObject_t* serial = objp;
    NbEfiSerialDev_t* dev = NbObjGetData (serial);
    uint8_t* c = (uint8_t*) params;
    // Wait for port to be ready
    uint32_t ctrl = 0;
    do
    {
        uefi_call_wrapper (dev->prot->GetControl, 2, dev->prot, &ctrl);
    } while (ctrl & EFI_SERIAL_INPUT_BUFFER_EMPTY);
    // Read from port
    size_t bufSz = 1;
    if (uefi_call_wrapper (dev->prot->Read, 3, dev->prot, &bufSz, c) != EFI_SUCCESS)
        return false;
    return true;
}

NbObjSvc efiSerialSvcs[] =
    {NULL, NULL, NULL, EfiSerialDumpData, EfiSerialNotify, EfiSerialWrite, EfiSerialRead};

NbObjSvcTab_t efiSerialSvcTab = {ARRAY_SIZE (efiSerialSvcs), efiSerialSvcs};

NbDriver_t efiSerialDrv = {"Rs232_Efi", EfiSerialEntry, {0}, 0, false, sizeof (NbEfiSerialDev_t)};

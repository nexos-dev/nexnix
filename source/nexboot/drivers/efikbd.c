/*
    efikbd.c - contains EFI keyboard driver
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
#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <string.h>

extern NbObjSvcTab_t efiKbdSvcTab;
extern NbDriver_t efiKbdDrv;

typedef struct _efikbddev
{
    NbHwDevice_t dev;                     // Standard header
    EFI_SIMPLE_TEXT_IN_PROTOCOL* prot;    // Protocol
} NbEfiKbdDev_t;

static bool initialized = false;    // If drivers has already been initialized

static bool EfiKbdEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {
            if (initialized)
                return false;    // Already did this
            NbEfiKbdDev_t* kbd = params;
            kbd->dev.devId = 0;
            kbd->dev.sz = sizeof (NbEfiKbdDev_t);
            kbd->prot = ST->ConIn;
            initialized = true;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &efiKbdSvcTab);
            NbObjSetManager (obj, &efiKbdDrv);
            break;
        }
    }
    return true;
}

static bool EfiKbdDumpData (void* objp, void* params)
{
    return true;
}

static bool EfiKbdNotify (void* objp, void* params)
{
    // Get notification code
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = params;
    int code = notify->code;
    if (code == NB_KEYBOARD_NOTIFY_SETOWNER)
    {
        // Notify current owner that we are being deteached
        if (obj->owner)
            obj->owner->entry (NB_DRIVER_ENTRY_DETACHOBJ, obj);
        NbDriver_t* newDrv = notify->data;
        // Set new owner
        NbObjSetOwner (obj, newDrv);
        // Attach it
        newDrv->entry (NB_DRIVER_ENTRY_ATTACHOBJ, obj);
    }
    return true;
}

// Key conversion table
static uint8_t efiScanToKey[] = {0,
                                 NB_KEY_UP,
                                 NB_KEY_DOWN,
                                 NB_KEY_RIGHT,
                                 NB_KEY_LEFT,
                                 NB_KEY_HOME,
                                 NB_KEY_END,
                                 0,
                                 NB_KEY_DELETE,
                                 NB_KEY_PGUP,
                                 NB_KEY_PGDN,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0};

const char* keyToEscCode[] =
    {"\e[5~", "\e[6~", "\e[A", "\e[C", "\e[B", "\e[3~", "\e[H", "\e[D", "\e[F"};

static bool EfiKbdReadKey (void* objp, void* params)
{
    NbObject_t* kbdObj = objp;
    NbKeyData_t* keyData = params;
    NbEfiKbdDev_t* dev = NbObjGetData (kbdObj);
    // Wait for a key
read:
    size_t idx = 0;
    uefi_call_wrapper (BS->WaitForEvent, 3, 1, &dev->prot->WaitForKey, &idx);
    // Read it
    EFI_INPUT_KEY key = {0};
    uefi_call_wrapper (dev->prot->ReadKeyStroke, 2, dev->prot, &key);
    // Put in NbKeyData
    keyData->c = (uint8_t) key.UnicodeChar;
    keyData->isBreak = false;
    keyData->flags = 0;
    if (keyData->c == '\r')
        keyData->c = '\n';
    // Check if this is an escape code
    if (!keyData->c)
    {
        keyData->isEscCode = true;
        uint8_t keyScan = efiScanToKey[key.ScanCode];
        if (!keyScan)
            goto read;    // Re-read
        keyData->c = keyScan;
        keyData->escCode = keyToEscCode[keyScan - 0xF1];
    }
    return true;
}

// Data structures
NbObjSvc efiKbdSvcs[] = {NULL, NULL, NULL, EfiKbdDumpData, EfiKbdNotify, EfiKbdReadKey};
NbObjSvcTab_t efiKbdSvcTab = {ARRAY_SIZE (efiKbdSvcs), efiKbdSvcs};
NbDriver_t efiKbdDrv = {"EfiKbd", EfiKbdEntry, {0}, 0, false, sizeof (NbEfiKbdDev_t)};

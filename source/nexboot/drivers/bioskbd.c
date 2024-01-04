/*
    bioskbd.c - contains BIOS keyboard driver
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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <string.h>

extern NbObjSvcTab_t biosKbdSvcTab;
extern NbDriver_t biosKbdDrv;

static bool kbdDetected = false;

static bool BiosKbdEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {
            // Nothing to do
            if (kbdDetected)
                return false;
            kbdDetected = true;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &biosKbdSvcTab);
            NbObjSetManager (obj, &biosKbdDrv);
            break;
        }
    }
    return true;
}

static bool BiosDumpData (void* objp, void* params)
{
    return true;
}

static bool BiosNotify (void* objp, void* params)
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

const char* keyToEscCode[] =
    {"\e[5~", "\e[6~", "\e[A", "\e[C", "\e[B", "\e[3~", "\e[H", "\e[D", "\e[F"};

static bool BiosReadKey (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbKeyData_t* keyData = params;
    keyData->isBreak = false;
    keyData->flags = 0;
    // Call interrupt to wait for key
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0;
    NbBiosCall (0x16, &in, &out);
    // Parse information
    if (out.al)
    {
        keyData->c = out.al;
        if (keyData->c == '\r')
            keyData->c = '\n';
    }
    else
    {
        keyData->isEscCode = true;
        // Parse special keys
        if (out.ah == 0x53)
            keyData->c = NB_KEY_DELETE;
        else if (out.ah == 0x4F)
            keyData->c = NB_KEY_END;
        else if (out.ah == 0x4B)
            keyData->c = NB_KEY_LEFT;
        else if (out.ah == 0x47)
            keyData->c = NB_KEY_HOME;
        else if (out.ah == 0x50)
            keyData->c = NB_KEY_DOWN;
        else if (out.ah == 0x4D)
            keyData->c = NB_KEY_RIGHT;
        else if (out.ah == 0x48)
            keyData->c = NB_KEY_UP;
        else if (out.ah == 0x51)
            keyData->c = NB_KEY_PGDN;
        else if (out.ah == 0x49)
            keyData->c = NB_KEY_PGUP;
        keyData->escCode = keyToEscCode[keyData->c - 0xF1];
    }
    return true;
}

static NbObjSvc biosKbdSvcs[] = {NULL, NULL, NULL, BiosDumpData, BiosNotify, BiosReadKey};

NbObjSvcTab_t biosKbdSvcTab = {ARRAY_SIZE (biosKbdSvcs), biosKbdSvcs};

NbDriver_t biosKbdDrv = {"BiosKbd", BiosKbdEntry, {0}, 0, false, sizeof (NbHwDevice_t)};

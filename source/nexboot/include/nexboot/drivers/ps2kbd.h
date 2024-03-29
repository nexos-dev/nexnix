/*
    ps2kbd.h - contains PS/2 keyboard stuff
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

#ifndef _PS2KBD_H
#define _PS2KBD_H

#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>

typedef struct _ps2kbd
{
    NbHwDevice_t hdr;     // Device header
    NbDriver_t* owner;    // Driver who currently owns keyboard
    int ledFlags;         // LED flags
    bool capsState;       // State of caps lock / shift
    bool shiftState;
    bool ctrlState;
    bool altState;
} NbPs2Kbd_t;

#define NB_DEVICE_SUBTYPE_PS2KBD 2

#endif

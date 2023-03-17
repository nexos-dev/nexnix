/*
    vgaconsole.h - contains VGA text console stuff
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

#ifndef _VGACONSOLE_H
#define _VGACONSOLE_H

#include <nexboot/driver.h>
#include <nexboot/drivers/console.h>
#include <nexboot/fw.h>

typedef struct _vgaConsole
{
    NbHwDevice_t hdr;     // Device header
    NbDriver_t* owner;    // Driver who currently owns console
    int rows;             // Console row amount
    int cols;             // Console column amount
    int mode;             // Console mode (currently on 80x25, 16 colors)
    int fgColor;          // Current foreground color
    int bgColor;          // Current background color
} NbVgaConsole_t;

#define NB_VGA_CONSOLE_MODE_03H 1
#define NB_VGA_CONSOLE_03H_ROWS 25
#define NB_VGA_CONSOLE_03H_COLS 80

#define NB_DEVICE_SUBTYPE_VGACONSOLE 1

// Color translation table
static uint8_t vgaColorTab[] = {0, 4, 2, 10, 1, 5, 3, 7};

#endif

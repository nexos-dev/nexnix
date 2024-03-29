/*
    drivers.h - contains driver definitions
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

#ifndef _DRIVERS_H
#define _DRIVERS_H

#include <nexboot/driver.h>
#include <nexboot/nexboot.h>
#include <stdbool.h>

// Driver pointers
extern NbDriver_t terminalDrv;
#ifdef NEXNIX_FW_BIOS
extern NbDriver_t vgaConsoleDrv;
extern NbDriver_t biosKbdDrv;
extern NbDriver_t uart16550Drv;
extern NbDriver_t biosDiskDrv;
extern NbDriver_t vbeDrv;
#elif defined NEXNIX_FW_EFI
extern NbDriver_t efiSerialDrv;
extern NbDriver_t efiKbdDrv;
extern NbDriver_t efiDiskDrv;
extern NbDriver_t gopDrv;
#endif
extern NbDriver_t volManagerDrv;
extern NbDriver_t textUiDrv;
extern NbDriver_t fbConsDrv;

// Driver tables

// Drivers started as soon as possible for devices
#ifdef NEXNIX_FW_BIOS
static NbDriver_t* nbPhase1DrvTab[] =
    {&volManagerDrv, &vgaConsoleDrv, &biosKbdDrv, &uart16550Drv, &biosDiskDrv, &vbeDrv};
#elif defined NEXNIX_FW_EFI
static NbDriver_t* nbPhase1DrvTab[] = {&volManagerDrv, &efiSerialDrv, &efiKbdDrv, &efiDiskDrv,
                                        &gopDrv};
#endif
static NbDriver_t* nbPhase2DrvTab[] = {&fbConsDrv, &terminalDrv, &textUiDrv};

#endif

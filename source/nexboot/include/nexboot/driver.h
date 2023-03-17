/*
    driver.h - contains driver definitions
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

#ifndef _DRIVER_H
#define _DRIVER_H

#include <nexboot/object.h>
#include <stdbool.h>
#include <stddef.h>

#define DRIVER_ATTACHED_OBJS 45
#define DRIVER_NUM_DEPS      12

typedef bool (*DrvEntry) (int, void*);

// nexboot driver structure
typedef struct _drv
{
    const char name[64];    //// Name of driver
    int devType;            /// Type of device. Corresponds device object interface
    int devSubType;         /// Subtype of device
    DrvEntry entry;         /// Driver entry
    const char deps[64][DRIVER_NUM_DEPS];    /// Driver dependencies
    size_t numDeps;                          /// Number of dependencies
    bool started;                            /// Has the driver been started
    size_t devSize;                          /// Size of device structure
} NbDriver_t;

/// Finds a driver
NbDriver_t* NbFindDriver (const char* name);

/// Starts a driver by name
bool NbStartDriver (const char* name);

/// Starts a driver by a pointer
bool NbStartDriverByPtr (NbDriver_t* drv);

/// Stops a driver
bool NbStopDriverByPtr (NbDriver_t* drv);

/// Stops a driver
bool NbStopDriver (const char* drv);

/// Starts phase 1 drivers
bool NbStartPhase1Drvs();

/// Sends driver a code
bool NbSendDriverCode (NbDriver_t* drv, int code, void* data);

#define NB_DRIVER_ENTRY_START     1
#define NB_DRIVER_ENTRY_ATTACHOBJ 2
#define NB_DRIVER_ENTRY_DETACHOBJ 3
#define NB_DRIVER_ENTRY_STOP      4
#define NB_DRIVER_ENTRY_DETECTHW  5

#endif

/*
    driver.c - contains driver management functions
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

#include "drivers.h"
#include <assert.h>
#include <nexboot/driver.h>
#include <string.h>

// Finds a driver
NbDriver_t* NbFindDriver (const char* name)
{
    // Search in phase 1table
    size_t numP1Drvs = ARRAY_SIZE (nbPhase1DrvTab);
    for (int i = 0; i < numP1Drvs; ++i)
    {
        if (!strcmp (nbPhase1DrvTab[i]->name, name))
            return nbPhase1DrvTab[i];
    }
    return NULL;
}

// Starts a driver by name
bool NbStartDriver (const char* name)
{
    NbDriver_t* drv = NbFindDriver (name);
    if (!drv)
        return false;
    return NbStartDriverByPtr (drv);
}

// Starts a driver by pointer
bool NbStartDriverByPtr (NbDriver_t* drv)
{
    assert (drv);
    if (drv->started)
        return false;
    // Start driver dependencies
    for (int i = 0; i < drv->numDeps; ++i)
    {
        if (drv->deps[i])
        {
            if (!NbStartDriver (drv->deps[i]))
                return false;
        }
    }
    drv->started = true;
    // Run init function
    return drv->entry (NB_DRIVER_ENTRY_START, NULL);
}

// Stops a driver
bool NbStopDriverByPtr (NbDriver_t* drv)
{
    if (!drv->started)
        return false;
    return drv->entry (NB_DRIVER_ENTRY_STOP, NULL);
}

// Stops a driver
bool NbStopDriver (const char* name)
{
    NbDriver_t* drv = NbFindDriver (name);
    if (!drv)
        return false;
    return NbStopDriverByPtr (drv);
}

// Starts phase 1 drivers
bool NbStartPhase1Drvs()
{
    size_t numDrvs = ARRAY_SIZE (nbPhase1DrvTab);
    for (int i = 0; i < numDrvs; ++i)
    {
        if (!nbPhase1DrvTab[i]->started)
        {
            if (!NbStartDriverByPtr (nbPhase1DrvTab[i]))
                return false;
        }
    }
    return true;
}

// Sends driver a code
bool NbSendDriverCode (NbDriver_t* drv, int code, void* data)
{
    return drv->entry (code, data);
}

/*
    pcdetect.c - contains PC hardware detection
    Copyright 2024 The NexNix Project

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

#include "pc.h"
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>

// Console struct externs
extern NkConsole_t vgaCons;
extern NkConsole_t uartCons;
extern NkConsole_t fbCons;

static NkConsole_t* primaryCons = NULL;      // Primary console
static NkConsole_t* secondaryCons = NULL;    // Secondary console

// Initialize boot drivers
void PltInitDrvs()
{
    NexNixBoot_t* boot = NkGetBootArgs();
    // Figure out whether we are in a graphical mode or VGA text mode
    if (boot->displayDefault)
    {
        // Initialize VGA text driver
        PltVgaInit();
        primaryCons = &vgaCons;    // This is the primary console
    }
    else
    {
        NkFbConsInit();
        primaryCons = &fbCons;
    }
    // Initialize UART
    if (PltUartInit())
    {
        if (!primaryCons)
            primaryCons = &uartCons;
        secondaryCons = &uartCons;
    }
    if (!PltAcpiInit())
        return;
}

// Gets primary console
NkConsole_t* PltGetPrimaryCons()
{
    return primaryCons;
}

// Gets secondary console
NkConsole_t* PltGetSecondaryCons()
{
    return secondaryCons;
}

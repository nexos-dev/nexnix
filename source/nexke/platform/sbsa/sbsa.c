/*
    sbsa.c - contains platform - specific components of nexke
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

#include "sbsa.h"
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>

// Console struct externs
extern NkConsole_t fbCons;
extern NkConsole_t pl011Cons;

static NkConsole_t* primaryCons = NULL;      // Primary console
static NkConsole_t* secondaryCons = NULL;    // Secondary console

// Sets up platform drivers
void PltInitDrvs()
{
    // Initialize framebuffer console
    if (!NkGetBootArgs()->displayDefault)
    {
        NkFbConsInit();
        primaryCons = &fbCons;
    }
    // Setup ACPI
    if (!PltAcpiInit())
    {
        // This is not an SBSA / EBBR compliant system.
        // Just crash
        primaryCons->write ("nexke: fatal error: system doesn't support ACPI");
        CpuCrash();
    }
    // Get DBG2 table to find serial port
    void* dbgTab = PltAcpiFindTable ("DBG2");
    if (dbgTab)
    {
        //  Grab table
        AcpiDbg2_t* dbg = dbgTab;
        // Iterate through each descriptor
        AcpiDbgDesc_t* desc = dbgTab + dbg->devDescOff;
        for (int i = 0; i < dbg->numDesc; ++i)
        {
            // Check type / subtype to see if it is supported
            if (desc->portType == ACPI_DBG_PORT_SERIAL)
            {
                // Check sub-type
                if (desc->portSubtype == ACPI_DBG_PORT_PL011)
                {
                    // Initialize it
                    AcpiGas_t* gas = (void*) desc + desc->barOffset;
                    PltPL011Init (gas);
                    if (!primaryCons)
                        primaryCons = &pl011Cons;
                    secondaryCons = &pl011Cons;
                }
            }
            desc = (void*) desc + desc->len;
        }
    }
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

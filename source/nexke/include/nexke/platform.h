/*
    platform.h - contains platform header
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

#ifndef _PLATFORM_H
#define _PLATFORM_H

#include <nexke/cpu.h>
#include <nexke/platform/acpi.h>
#include <nexke/types.h>
#include <stdbool.h>

// Initialize boot drivers
void PltInitDrvs();

// Early console structure
typedef struct _nkcons
{
    bool (*read) (char*);           // Reads a character from the console
    void (*write) (const char*);    // Writes a string to the console
} NkConsole_t;

// Gets primary console
NkConsole_t* PltGetPrimaryCons();

// Gets secondary console
NkConsole_t* PltGetSecondaryCons();

// Interrupt manager

// IPLs
#define PLT_NO_IPL   -1
#define PLT_IPL_LOW  0
#define PLT_IPL_HIGH 32

// Function pointer types for below
typedef bool (*PltHwCheckSpurious) (NkCcb_t*, NkInterrupt_t*);
typedef void (*PltHwEndInterrupt) (NkCcb_t*, NkInterrupt_t*);
typedef void (*PltHwDisableInterrupt) (NkCcb_t*, NkInterrupt_t*);
typedef void (*PltHwEnableInterrupt) (NkCcb_t*, NkInterrupt_t*);
typedef void (*PltHwRaiseIpl) (NkCcb_t*, ipl_t);
typedef void (*PltHwLowerIpl) (NkCcb_t*, ipl_t);
typedef void (*PltConnectInterrupt) (NkCcb_t*, NkInterrupt_t*);
typedef void (*PltDisconnectInterrupt) (NkCcb_t*, NkInterrupt_t*);

// Hardware interrupt controller data structure
typedef struct _hwintctrl
{
    int type;
    PltHwCheckSpurious checkSpurious;
    PltHwEndInterrupt endInterrupt;
    PltHwDisableInterrupt disableInterrupt;
    PltHwEnableInterrupt enableInterrupt;
    PltHwRaiseIpl raiseIpl;
    PltHwLowerIpl lowerIpl;
    PltConnectInterrupt connectInterrupt;
    PltDisconnectInterrupt disconnectInterrupt;
} PltHwIntCtrl_t;

// Valid controller types
#define PLT_HWINT_8295A 1
#define PLT_HWINT_APIC  2

// Interrupt function type
typedef bool (*PltIntHandler) (NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Interrupt object
typedef struct _int
{
    int vector;               // Interrupt vector number
    int type;                 // Is this an exception, a service, or an external interrupt?
    ipl_t ipl;                // IPL this interrupt should run at. -1 means IPL doesn't apply
    long long callCount;      // Number of times this interrupt has been called
    PltIntHandler handler;    // Interrupt handler function
} NkInterrupt_t;

#define PLT_INT_EXEC  0
#define PLT_INT_SVC   1
#define PLT_INT_HWINT 2

// Called when a trap goes bad
void PltBadTrap (CpuIntContext_t* context, const char* msg, ...);

// Raises IPL to specified level
ipl_t PltRaiseIpl (ipl_t newIpl);

// Lowers IPL back to level
void PltLowerIpl (ipl_t oldIpl);

// Initializes interrupt system
void PltInitInterrupts();

// Installs an interrupt handler
NkInterrupt_t* PltInstallInterrupt (int vector, int type, ipl_t ipl, PltIntHandler hndlr);

// Uninstalls an interrupt handler
void PltUninstallInterrupt (NkInterrupt_t* intObj);

#endif

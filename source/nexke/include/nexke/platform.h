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
#include <stddef.h>

// Initialize boot drivers
void PltInitDrvs();

// Initialize phase 2
void PltInitPhase2();

// Initialize phase 3
void PltInitPhase3();

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
#define PLT_IPL_LOW   0
#define PLT_IPL_CLOCK 30
#define PLT_IPL_HIGH  31

// Function pointer types for below
typedef bool (*PltHwBeginInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwEndInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwDisableInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwEnableInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwSetIpl) (NkCcb_t*, ipl_t);
typedef int (*PltHwConnectInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwDisconnectInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef int (*PltHwMapInterrupt) (int);

// Hardware interrupt controller data structure
typedef struct _hwintctrl
{
    int type;
    PltHwBeginInterrupt beginInterrupt;
    PltHwEndInterrupt endInterrupt;
    PltHwDisableInterrupt disableInterrupt;
    PltHwEnableInterrupt enableInterrupt;
    PltHwSetIpl setIpl;
    PltHwConnectInterrupt connectInterrupt;
    PltHwDisconnectInterrupt disconnectInterrupt;
    PltHwMapInterrupt mapInterrupt;
} PltHwIntCtrl_t;

// Valid controller types
#define PLT_HWINT_8259A 1
#define PLT_HWINT_APIC  2

// Initializes system inerrupt controller
PltHwIntCtrl_t* PltInitHwInts();

// Hardware interrupt
typedef struct _hwint
{
    int line;     // Line number
    int flags;    // Interrupt flags
    int mode;     // Level or edge
    ipl_t ipl;    // IPL value
} NkHwInterrupt_t;

#define PLT_MODE_EDGE  0
#define PLT_MODE_LEVEL 1

// Interrupt function type
typedef bool (*PltIntHandler) (NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Interrupt object
typedef struct _int
{
    int vector;               // Interrupt vector number
    int type;                 // Is this an exception, a service, or an external interrupt?
    long long callCount;      // Number of times this interrupt has been called
    PltIntHandler handler;    // Interrupt handler function
    NkHwInterrupt_t hwInt;    // Hardware interrupt structure for hardware ints
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
NkInterrupt_t* PltInstallInterrupt (int vector,
                                    int type,
                                    PltIntHandler hndlr,
                                    NkHwInterrupt_t* hwInt);

// Uninstalls an interrupt handler
void PltUninstallInterrupt (NkInterrupt_t* intObj);

// Connects an interrupt to hardware controller. Returns a vector to install it to
int PltConnectInterrupt (NkHwInterrupt_t* hwInt);

// Clock system

typedef uint64_t (*PltHwGetTime)();

// Hardware clock
typedef struct _hwclock
{
    int type;                  // Underlying device
    int precision;             // Precision in nanoseconds
    PltHwGetTime getTime;      // Gets current time on clock
    uint64_t internalCount;    // Used for software clocking on some systems
    uintptr_t private;
} PltHwClock_t;

#define PLT_CLOCK_PIT  1
#define PLT_CLOCK_ACPI 2

// Initializes clock system
PltHwClock_t* PltInitClock();

// Timer system

typedef void (*PltHwSetTimerCallback) (void (*)());
typedef void (*PltHwArmTimer) (uint64_t);

// Hardware timer data structure
typedef struct _hwtimer
{
    int type;                // Timer type
    int precision;           // Precision of timer in nanoseconds
    uint64_t maxInterval;    // Max interval we can be armed for
    // Private data
    void (*callback)();    // Interrupt callback
    uintptr_t private;
    // Function interface
    PltHwArmTimer armTimer;
    PltHwSetTimerCallback setCallback;
} PltHwTimer_t;

#define PLT_TIMER_PIT  1
#define PLT_TIMER_SOFT 2

// Initializes system timer
PltHwTimer_t* PltInitTimer();

// Nanoseconds in a second
#define PLT_NS_IN_SEC 1000000000

// Platform structure
typedef struct _nkplt
{
    int type;    // Type of platform
    int subType;
    NkConsole_t* primaryCons;    // Consoles
    NkConsole_t* secondaryCons;
    PltHwClock_t* clock;        // System clock
    PltHwTimer_t* timer;        // System timer
    PltHwIntCtrl_t* intCtrl;    // Interrupt controller
    // ACPI related things
    int acpiVer;                   // ACPI version
    AcpiRsdp_t rsdp;               // Copy of RSDP
    AcpiCacheEnt_t* tableCache;    // ACPI table cache
} NkPlatform_t;

#define PLT_TYPE_PC   1
#define PLT_TYPE_SBSA 2

#define PLT_PC_SUBTYPE_ACPI 1
#define PLT_PC_SUBTYPE_MPS  2
#define PLT_PC_SUBTYPE_ISA  3

// Gets platform
NkPlatform_t* PltGetPlatform();

#endif

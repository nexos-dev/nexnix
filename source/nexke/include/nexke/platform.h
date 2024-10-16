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
#define PLT_IPL_TIMER 32
#define PLT_IPL_HIGH  33

// Function pointer types for below
typedef bool (*PltHwBeginInterrupt) (NkCcb_t*, int vector);
typedef void (*PltHwEndInterrupt) (NkCcb_t*, int vector);
typedef void (*PltHwDisableInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwEnableInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwSetIpl) (NkCcb_t*, ipl_t);
typedef int (*PltHwConnectInterrupt) (NkCcb_t*, NkHwInterrupt_t*);
typedef void (*PltHwDisconnectInterrupt) (NkCcb_t*, NkHwInterrupt_t*);

// Interupt chain structure
typedef struct _intchain
{
    NkList_t list;
    size_t chainLen;
    size_t maskCount;    // Number of masked interrupts in chain
    bool noRemap;        // If the chain is remappable
} PltHwIntChain_t;

// Hardware interrupt controller data structure
typedef struct _hwintctrl
{
    int type;
    PltHwIntChain_t* lineMap;    // Map of all interrupt lines
    size_t mapEntries;           // Number of map entries
    PltHwBeginInterrupt beginInterrupt;
    PltHwEndInterrupt endInterrupt;
    PltHwDisableInterrupt disableInterrupt;
    PltHwEnableInterrupt enableInterrupt;
    PltHwSetIpl setIpl;
    PltHwConnectInterrupt connectInterrupt;
    PltHwDisconnectInterrupt disconnectInterrupt;
} PltHwIntCtrl_t;

// Valid controller types
#define PLT_HWINT_8259A 1
#define PLT_HWINT_APIC  2

// Initializes system inerrupt controller
PltHwIntCtrl_t* PltInitHwInts();

// Interrupt function type
typedef bool (*PltIntHandler) (NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Hardware interrupt
typedef struct _hwint
{
    uint32_t gsi;             // GSI number
    int flags;                // Interrupt flags
    int mode;                 // Level or edge
    ipl_t ipl;                // IPL value
    int vector;               // Vector we're connected to
    PltIntHandler handler;    // Handler for this interrupt
    NkLink_t link;
} NkHwInterrupt_t;

#define PLT_MODE_EDGE  0
#define PLT_MODE_LEVEL 1

#define PLT_HWINT_INTERNAL      (1 << 0)
#define PLT_HWINT_ACTIVE_LOW    (1 << 2)
#define PLT_HWINT_NON_CHAINABLE (1 << 3)
#define PLT_HWINT_CHAINED       (1 << 4)
#define PLT_HWINT_FORCE_IPL     (1 << 5)
#define PLT_HWINT_MASKED        (1 << 6)

#define PLT_GSI_INTERNAL 0xFFFFFFFF

// Interrupt object
typedef struct _int
{
    int vector;             // Interrupt vector number
    int type;               // Is this an exception, a service, or an external interrupt?
    long long callCount;    // Number of times this interrupt has been called
    union
    {
        PltIntHandler handler;        // Interrupt handler function
        PltHwIntChain_t* intChain;    // Interrupt chain
    };
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

// Installs an exception handler
NkInterrupt_t* PltInstallExec (int vector, PltIntHandler hndlr);

// Installs a service handler
NkInterrupt_t* PltInstallSvc (int vector, PltIntHandler hndlr);

// Installs a hardware interrupt
NkInterrupt_t* PltInstallInterrupt (int vector, NkHwInterrupt_t* intObj);

// Uninstalls an interrupt handler
void PltUninstallInterrupt (NkInterrupt_t* intObj);

// Connects an interrupt to hardware controller. Returns a vector to install it to
int PltConnectInterrupt (NkHwInterrupt_t* hwInt);

// Disconnects interrupt from hardware controller
void PltDisconnectInterrupt (NkHwInterrupt_t* hwInt);

// Enables an interrupt
void PltEnableInterrupt (NkHwInterrupt_t* hwInt);

// Disables an interrupt
void PltDisableInterrupt (NkHwInterrupt_t* hwInt);

// Remaps hardware interrupts on specified object to a new vector and IPL
// Requires input to be a hardware interrupt object, and returns the new interrupt
// Called with interrupts disabled
NkInterrupt_t* PltRemapInterrupt (NkInterrupt_t* oldInt, int newVector, ipl_t newIpl);

// Allocates a hardware interrupt
NkHwInterrupt_t* PltAllocHwInterrupt();

// Checks if two hardware interrupts are compatible
bool PltAreIntsCompatible (NkHwInterrupt_t* int1, NkHwInterrupt_t* int2);

// Retrieves interrupt obejct from table
NkInterrupt_t* PltGetInterrupt (int vector);

// Clock system

typedef ktime_t (*PltHwGetTime)();
typedef void (*PltHwPoll) (ktime_t time);

// Hardware clock
typedef struct _hwclock
{
    int type;                 // Underlying device
    int precision;            // Precision in nanoseconds
    PltHwGetTime getTime;     // Gets current time on clock
    PltHwPoll poll;           // Polls for a amount of time;
    ktime_t internalCount;    // Used for software clocking on some systems
    uintptr_t private;
} PltHwClock_t;

#define PLT_CLOCK_PIT  1
#define PLT_CLOCK_ACPI 2
#define PLT_CLOCK_HPET 3
#define PLT_CLOCK_TSC  4

// Initializes clock system
PltHwClock_t* PltInitClock();

// Timer system

typedef void (*PltHwSetTimerCallback) (void (*)());
typedef void (*PltHwArmTimer) (ktime_t);

// Hardware timer data structure
typedef struct _hwtimer
{
    int type;               // Timer type
    int precision;          // Precision of timer in nanoseconds
    ktime_t maxInterval;    // Max interval we can be armed for
    // Private data
    void (*callback)();    // Interrupt callback
    uintptr_t private;
    // Function interface
    PltHwArmTimer armTimer;
    PltHwSetTimerCallback setCallback;
} PltHwTimer_t;

#define PLT_TIMER_PIT  1
#define PLT_TIMER_SOFT 2
#define PLT_TIMER_APIC 3
#define PLT_TIMER_HPET 4
#define PLT_TIMER_TSC  5

// Initializes system timer
PltHwTimer_t* PltInitTimer();

// Nanoseconds in a second
#define PLT_NS_IN_SEC 1000000000

// CPU system

typedef struct _hwcpu
{
    int id;      // ID according to platform
    int type;    // CPU interrupt controller type
    NkLink_t link;
} PltCpu_t;

static const char* pltCpuTypes[] = {"APIC", "x2APIC", "none"};

#define PLT_CPU_APIC   0
#define PLT_CPU_X2APIC 1
#define PLT_CPU_UP     2    // CPU has no embedded interrupt controller

// Interrupt overrides
typedef struct _hwintsrc
{
    int line;        // Line on bus
    int bus;         // Bus attached to
    int mode;        // Trigger mode
    int polarity;    // Polarity of interrupt
    uint32_t gsi;    // Global system inerrupt of interrupt
    NkLink_t link;
} PltIntOverride_t;

#define PLT_POL_ACTIVE_HIGH 0
#define PLT_POL_ACTIVE_LOW  1

static const char* pltBusTypes[] = {"ISA"};

#define PLT_BUS_ISA 0

typedef struct _hwintctl
{
    int type;            // Type
    int id;              // ID of this controller
    uint64_t addr;       // ADdress of it
    uint32_t gsiBase;    // Base interrupt number
    NkLink_t link;
} PltIntCtrl_t;

static const char* pltIntCtrlTypes[] = {"IOAPIC", "8259A"};

#define PLT_INTCTRL_IOAPIC 0
#define PLT_INTCTRL_8259A  1

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
    NkList_t cpus;              // List of CPUs
    PltCpu_t* bsp;              // BSP CPU
    NkList_t ints;              // List of interrupt sources
    NkList_t intCtrls;          // List of interrupt controllers
    int numCpus;
    int numIntCtrls;
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

// Adds CPU to platform
void PltAddCpu (PltCpu_t* cpu);

// Adds interrupt to platform
void PltAddInterrupt (PltIntOverride_t* intSrc);

// Adds interrupt controller to platform
void PltAddIntCtrl (PltIntCtrl_t* intCtrl);

// Resolves an interrupt line from bus-specific to a GSI
uint32_t PltGetGsi (int bus, int line);

// Gets an interrupt override based on the GSI
PltIntOverride_t* PltGetOverride (uint32_t gsi);

#endif

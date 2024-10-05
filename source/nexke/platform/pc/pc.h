/*
    pc.h - contains PC internal functions
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

#ifndef _PC_H
#define _PC_H

#include <nexke/platform.h>
#include <stdbool.h>

// Initializes VGA console
void PltVgaInit();

// Initializes UART 16550 driver
bool PltUartInit();

// Initializes 8259A PIC
PltHwIntCtrl_t* PltPicInit();

// Initializes I/O and Local APICs
PltHwIntCtrl_t* PltApicInit();

// PIC IRQ lines
#define PLT_PIC_IRQ_PIT 0

// Initializes PIT clock part
PltHwClock_t* PltPitInitClk();

// Initializes PIT timer part
PltHwTimer_t* PltPitInitTimer();

// Enable ACPI
void PltAcpiPcEnable();

// Initialize APIC timer
PltHwTimer_t* PltApicInitTimer();

#endif

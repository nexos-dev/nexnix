/*
    pl011.c - conatins ARM PL011 serial driver
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
#include <nexke/nexke.h>
#include <nexke/platform.h>

// Register offsets
#define PL011_DR    0
#define PL011_RSR   4
#define PL011_RCR   4
#define PL011_FR    0x18
#define PL011_LPR   0x20
#define PL011_IBRD  0x24
#define PL011_FBRD  0x28
#define PL011_LCR   0x2C
#define PL011_CR    0x30
#define PL011_FLS   0x34
#define PL011_IMSC  0x38
#define PL011_RIS   0x3C
#define PL011_MIS   0x40
#define PL011_ICE   0x44
#define PL011_DMACR 0x48

// Flag register defines
#define PL011_FR_BUSY    (1 << 3)
#define PL011_FR_RXEMPTY (1 << 4)

// LCR defines
#define PL011_LCR_8BITS (3 << 5)

// UART CR defines
#define PL011_CR_UARTEN (1 << 0)
#define PL011_CR_TXEN   (1 << 8)
#define PL011_CR_RXEN   (1 << 9)
#define PL011_CR_RTS    (1 << 14)
#define PL011_CR_CTS    (1 << 15)

// PL011 base
static uintptr_t pl011Base = 0;

bool PltPL011Init (AcpiGas_t* gas)
{
    // Set base address
    if (gas->asId)
        return false;
    pl011Base = gas->addr;
    // Get SPCR
    AcpiSpcr_t* spcr = (AcpiSpcr_t*) PltAcpiFindTable ("SPCR");
    if (!spcr)
        return false;    // No way to determine baud rate
    // Determine clock
    uint32_t clock = 0;
    if (spcr->sdt.rev > 2 && spcr->uartClock)
        clock = spcr->uartClock;
    else
    {
        // Figure out baud rate FW left us in
        uint32_t baud = 0;
        if (spcr->sdt.rev > 2 && spcr->preciseBaud)
            baud = spcr->preciseBaud;
        else
        {
            if (spcr->baudRate == 0)
                ;    // Do nothing, OS will leave divisor as is
            else if (spcr->baudRate == 3)
                baud = 9600;
            else if (spcr->baudRate == 4)
                baud = 19200;
            else if (spcr->baudRate == 6)
                baud = 57600;
            else if (spcr->baudRate == 7)
                baud = 115200;
        }
        if (baud)
        {
            // Get divisor from registers
        }
        // If baud is 0, driver just won't mess with divisor
    }
    return true;
}

NkConsole_t pl011Cons = {.read = NULL, .write = NULL};

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
#include <nexke/mm.h>
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
#define PL011_FR_TXEMPTY (1 << 7)
#define PL011_FR_RXEMPTY (1 << 4)

// LCR defines
#define PL011_LCR_8BITS (3 << 5)

// UART CR defines
#define PL011_CR_UARTEN (1 << 0)
#define PL011_CR_TXEN   (1 << 8)
#define PL011_CR_RXEN   (1 << 9)
#define PL011_CR_RTS    (1 << 14)
#define PL011_CR_CTS    (1 << 15)

// Baud rate
#define PL011_DEFAULT_BAUDRATE 38400

// PL011 base
static uintptr_t pl011Base = NEXKE_SERIAL_MMIO_BASE;

// Reads a PL011 register
static uint32_t pl011ReadReg (int reg)
{
    volatile uint32_t* regPtr = (volatile uint32_t*) (pl011Base + reg);
    return *regPtr;
}

// Writes a pl011 register
static void pl011WriteReg (int reg, uint32_t val)
{
    volatile uint32_t* regPtr = (volatile uint32_t*) (pl011Base + reg);
    *regPtr = val;
}

static void pl011WaitForTx()
{
    while (!(pl011ReadReg (PL011_FR) & PL011_FR_TXEMPTY))
        ;
}

static void pl011WaitForRx()
{
    while (!(pl011ReadReg (PL011_FR) & PL011_FR_RXEMPTY))
        ;
}

bool PltPL011Init (AcpiGas_t* gas)
{
    // Set base address
    if (gas->asId)
        return false;
    uintptr_t pl011Phys = gas->addr;
    // Map it
    MmMulMapEarly (pl011Base, pl011Phys, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_CD);
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
            uint32_t idiv = pl011ReadReg (PL011_IBRD);
            uint32_t fdiv = pl011ReadReg (PL011_FBRD);
            uint32_t div = fdiv | (idiv << 6);
            // Compute clock rate by multiplying divisor by current baud.
            // This feels slightly hacky, but it works
            clock = div * baud;
        }
        // If we can't determine baud rate, we won't touch the divisor
        // This means the baud rate will be left at whatever FW set it to
    }
    // We now have the clock rate, we must now setup the registers
    // Disable for now
    pl011WriteReg (PL011_CR, pl011ReadReg (PL011_CR) & ~(PL011_CR_UARTEN));
    // Determine baud rate
    int divisor = 0;
    const char* baudArg = NkReadArg ("-baud");
    if (baudArg && *baudArg)
        divisor = clock / (atoi (baudArg) * 4);
    else
        divisor = clock / PL011_DEFAULT_BAUDRATE;
    // Split up into fractional and integer parts
    uint32_t idiv = (divisor >> 6) & 0xFFFF;
    uint32_t fdiv = divisor & 0x3F;
    pl011WriteReg (PL011_IBRD, idiv);
    pl011WriteReg (PL011_FBRD, fdiv);
    // Set up LCR
    pl011WriteReg (PL011_LCR, PL011_LCR_8BITS);
    // Set CR
    pl011WriteReg (PL011_CR, PL011_CR_RXEN | PL011_CR_TXEN | PL011_CR_CTS | PL011_CR_RTS);
    // Set interrupt masks
    pl011WriteReg (PL011_IMSC, 0x3FF);
    // Set DMA
    pl011WriteReg (PL011_DMACR, 0);
    // Enable
    pl011WriteReg (PL011_CR, pl011ReadReg (PL011_CR) | PL011_CR_UARTEN);
    return true;
}

static bool pl011Read (char* out)
{
    pl011WaitForRx();
    *s = pl011ReadReg (PL011_DR);
    if (*s == '\r')
        *s = '\n';    // Translate CR to LF
    return true;
}

static void pl011Write (const char* s)
{
    while (*s)
    {
        // Translate CRLF
        if (*s == '\n')
        {
            pl011WaitForTx();
            pl011WriteReg (PL011_DR, '\r');
        }
        pl011WaitForTx();
        pl011WriteReg (PL011_DR, *s);
        ++s;
    }
}

NkConsole_t pl011Cons = {.read = pl011Read, .write = pl011Write};

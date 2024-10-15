/*
    earlycons.c - contains console drivers
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

#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// VGA driver

// VGA memory base
#define VGA_MEMBASE_PHYS 0xB8000
#define VGA_MEMBASE      NEXKE_FB_BASE

// VGA colors we use
#define VGA_COLOR_BLACK      0
#define VGA_COLOR_LIGHT_GREY 7

// VGA memory access macros
#define VGA_MAKECOLOR(bg, fg)   (((bg) << 4) | (fg))
#define VGA_MAKEENTRY(c, color) ((c) | ((color) << 8))

// VGA width / height
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

// CRTC macros
#define VGA_CRTC_INDEX              0x3D4
#define VGA_CRTC_DATA               0x3D5
#define VGA_CRTC_INDEX_CURSOR_START 0x0A
#define VGA_CRTC_INDEX_CURSOR_END   0x0B
#define VGA_CRTC_INDEX_CURSOR_HIGH  0x0E
#define VGA_CRTC_INDEX_CURSOR_LOW   0x0F

// Is VGA working?
static bool isVgaWorking = false;

// Col / row state
static int curCol = 0, curRow = 0;

// Moves the cursor
static inline void vgaMoveCursor (int col, int row)
{
    uint16_t location = (row * VGA_WIDTH) + col;
    CpuOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_LOW);
    CpuIoWait();
    CpuOutb (VGA_CRTC_DATA, (uint8_t) (location & 0xFF));
    CpuIoWait();
    CpuOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_HIGH);
    CpuIoWait();
    CpuOutb (VGA_CRTC_DATA, (uint8_t) ((location >> 8) & 0xFF));
    CpuIoWait();
}

// Puts a character on screen on location
static inline void vgaPutChar (char c, int col, int row)
{
    uint16_t* vgaBase = (uint16_t*) VGA_MEMBASE;
    vgaBase[(row * VGA_WIDTH) + col] =
        VGA_MAKEENTRY (c, VGA_MAKECOLOR (VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY));
}

// Scrolls up one line
static inline void vgaScroll()
{
    uint16_t* vgaBase = (uint16_t*) VGA_MEMBASE;
    // Copy rows backwards
    for (int i = 0; i < (VGA_HEIGHT - 1); ++i)
    {
        memcpy (vgaBase, vgaBase + VGA_WIDTH, VGA_WIDTH * 2);
        vgaBase += VGA_WIDTH;
    }
    // Write out spaces
    for (int i = 0; i < VGA_WIDTH; ++i)
        vgaPutChar (' ', i, VGA_HEIGHT - 1);
}

// Write a characther to next available spot, performing some processing
static inline void vgaPrintChar (char c)
{
    if (c == '\n')
        curCol = 0, ++curRow;    // New line
    else if (c == '\r')
        curCol = 0;    // Carriage return
    else if (c == '\t')
        curCol &= ~(4 - 1), curCol += 4;    // Move to next spot divisible by 4
    else if (c == '\b')
    {
        --curCol;
        // Handle underflow
        if (curCol < 0)
        {
            if (curRow)
            {
                curCol = VGA_WIDTH - 1;
                --curRow;
            }
            else
                curCol = 0;    // Don't go off screen
        }
    }
    else
    {
        vgaPutChar (c, curCol, curRow);
        ++curCol;
    }
    // Handle row overflow
    if (curCol >= VGA_WIDTH)
    {
        curCol = 0;
        ++curRow;
    }
    // Decide if we need to scroll
    if (curRow >= VGA_HEIGHT)
    {
        vgaScroll();
        curRow = VGA_HEIGHT - 1;
    }
    // Move cursor
    vgaMoveCursor (curCol, curRow);
}

// Writes out a string
void vgaWriteString (const char* s)
{
    if (!isVgaWorking)
        return;
    while (*s)
        vgaPrintChar (*s), ++s;
}

// Stub read function
bool vgaRead (char*)
{
    return false;
}

// Initialize VGA console
void PltVgaInit()
{
    // Map VGA buffer
    MmMulMapEarly (VGA_MEMBASE, VGA_MEMBASE_PHYS, MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_WT);
    // Clear it
    for (int row = 0; row < VGA_HEIGHT; ++row)
    {
        for (int col = 0; col < VGA_WIDTH; ++col)
            vgaPutChar (' ', col, row);
    }
    vgaMoveCursor (1, 0);
    isVgaWorking = true;
}

// VGA console registration
NkConsole_t vgaCons = {.read = vgaRead, .write = vgaWriteString};

// 16550 driver

// Register definitions
#define UART_RXBUF            0
#define UART_TXBUF            0
#define UART_INT_ENABLE_REG   1
#define UART_INT_IDENT_REF    2
#define UART_FIFO_CTRL_REG    2
#define UART_LINE_CTRL_REG    3
#define UART_MODEM_CTRL_REG   4
#define UART_LINE_STATUS_REG  5
#define UART_MODEM_STATUS_REG 6
#define UART_SCRATCH_REG      7
#define UART_DIVISOR_LSB_REG  0
#define UART_DIVISOR_MSB_REG  1

// FCR bits
#define UART_FIFO_ENABLE   (1 << 0)
#define UART_FIFO_RX_RESET (1 << 1)
#define UART_FIFO_TX_RESET (1 << 2)

// LCR bits
#define UART_LCR_5BITS 0
#define UART_LCR_6BITS (1 << 0)
#define UART_LCR_7BITS (2 << 0)
#define UART_LCR_8BITS (3 << 0)
#define UART_LCR_1STOP (0 << 2)
#define UART_LCR_2STOP (1 << 2)
#define UART_LCR_DLAB  (1 << 7)

// LSR bits
#define UART_LSR_RXREADY (1 << 0)
#define UART_LSR_TXREADY (1 << 5)

// MCR bits
#define UART_MCR_DTS      (1 << 0)
#define UART_MCR_RTS      (1 << 1)
#define UART_MCR_LOOPBACK (1 << 4)

// Freqeuncy of UART crystal
#define UART_FREQUENCY        115200
#define UART_DEFAULT_BAUDRATE 38400

#define UART_IOBASE 0x3F8

// UART helper functions
static inline void uartWriteReg (uint8_t reg, uint8_t data)
{
    CpuOutb (UART_IOBASE + reg, data);
}

static inline uint8_t uartReadReg (uint8_t reg)
{
    return CpuInb (UART_IOBASE + reg);
}

// Waits for data to be ready
static inline void uartWaitForTx()
{
    while (!(uartReadReg (UART_LINE_STATUS_REG) & UART_LSR_TXREADY))
        ;
}

static inline void uartWaitForRx()
{
    while (!(uartReadReg (UART_LINE_STATUS_REG) & UART_LSR_RXREADY))
        ;
}

// Initializes UART driver
bool PltUartInit()
{
    // Program FIFO
    uartWriteReg (UART_FIFO_CTRL_REG, UART_FIFO_ENABLE | UART_FIFO_TX_RESET | UART_FIFO_RX_RESET);
    // Clear all interrupts
    uartWriteReg (UART_INT_ENABLE_REG, 0);
    // Write MCR
    uartWriteReg (UART_MODEM_CTRL_REG, UART_MCR_DTS | UART_MCR_RTS | UART_MCR_LOOPBACK);
    // Write LCR. We set DLAB here so we can set the divisor, and then clear
    // DLAB
    uartWriteReg (UART_LINE_CTRL_REG, UART_LCR_8BITS | UART_LCR_1STOP | UART_LCR_DLAB);
    // Set divisor
    int divisor = 0;
    const char* baudArg = NkReadArg ("-baud");
    if (baudArg && *baudArg)
        divisor = UART_FREQUENCY / atoi (baudArg);
    else
        divisor = UART_FREQUENCY / UART_DEFAULT_BAUDRATE;
    uartWriteReg (UART_DIVISOR_LSB_REG, divisor & 0xFF);
    uartWriteReg (UART_DIVISOR_MSB_REG, divisor >> 8);
    // Clear DLAB
    uartWriteReg (UART_LINE_CTRL_REG, uartReadReg (UART_LINE_CTRL_REG) & ~(UART_LCR_DLAB));
    // Test serial port
    uartWaitForTx();
    uartWriteReg (UART_TXBUF, 0x34);
    if (uartReadReg (UART_RXBUF) != 0x34)
        return false;
    // Test another byte
    uartWaitForTx();
    uartWriteReg (UART_TXBUF, 0x27);
    if (uartReadReg (UART_RXBUF) != 0x27)
        return false;
    // Clear loopback mode
    uartWriteReg (UART_MODEM_CTRL_REG, uartReadReg (UART_MODEM_CTRL_REG) & ~(UART_MCR_LOOPBACK));
    return true;
}

// Writes a string to UART
static void uartWrite (const char* s)
{
    while (*s)
    {
        // Translate CRLF
        if (*s == '\n')
        {
            uartWaitForTx();
            uartWriteReg (UART_TXBUF, '\r');
        }
        uartWaitForTx();
        uartWriteReg (UART_TXBUF, *s);
        ++s;
    }
}

// Reads a string from UART
static bool uartRead (char* s)
{
    uartWaitForRx();
    *s = uartReadReg (UART_RXBUF);
    if (*s == '\r')
        *s = '\n';    // Translate CR to LF
    return true;
}

// Console definition
NkConsole_t uartCons = {.read = uartRead, .write = uartWrite};

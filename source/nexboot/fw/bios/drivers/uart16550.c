/*
    uart16550.c - contains 16550 UART driver
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

#include <nexboot/driver.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/drivers/uart16550.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>

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

// Current COM port we are checking
static int curCom = 0;

extern NbObjSvcTab_t uart16550SvcTab;

// UART helper functions
static inline void uartWriteReg (NbUart16550Dev_t* dev, uint8_t reg, uint8_t data)
{
    NbOutb (dev->port + reg, data);
    NbIoWait();
}

static inline uint8_t uartReadReg (NbUart16550Dev_t* dev, uint8_t reg)
{
    return NbInb (dev->port + reg);
}

// Waits for data to be ready
static inline void uartWaitForTx (NbUart16550Dev_t* dev)
{
    while (!(uartReadReg (dev, UART_LINE_STATUS_REG) & UART_LSR_TXREADY))
        ;
}

static inline void uartWaitForRx (NbUart16550Dev_t* dev)
{
    while (!(uartReadReg (dev, UART_LINE_STATUS_REG) & UART_LSR_RXREADY))
        ;
}

static bool Uart16550Entry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {
            NbUart16550Dev_t* dev = params;
            // Get port base of this device via the BDA
            uint16_t* bda = (uint16_t*) 0x400;
            uint16_t portBase = bda[curCom];
            if (!portBase)
                return false;    // No COM port here
            dev->port = portBase;
            NbLogMessageEarly ("nbuart16550: found port COM%d at port base %x",
                               curCom,
                               portBase);
            ++curCom;
            // We have port base, begin initializing it
            // Program FIFO
            uartWriteReg (
                dev,
                UART_FIFO_CTRL_REG,
                UART_FIFO_ENABLE | UART_FIFO_TX_RESET | UART_FIFO_RX_RESET);
            // Clear all interrupts
            uartWriteReg (dev, UART_INT_ENABLE_REG, 0);
            // Write MCR
            uartWriteReg (dev,
                          UART_MODEM_CTRL_REG,
                          UART_MCR_DTS | UART_MCR_RTS | UART_MCR_LOOPBACK);
            // Write LCR. We set DLAB here so we can set the divisor, and then clear
            // DLAB
            uartWriteReg (dev,
                          UART_LINE_CTRL_REG,
                          UART_LCR_8BITS | UART_LCR_1STOP | UART_LCR_DLAB);
            // Set divisor
            int divisor = UART_FREQUENCY / UART_DEFAULT_BAUDRATE;
            uartWriteReg (dev, UART_DIVISOR_LSB_REG, divisor & 0xFF);
            uartWriteReg (dev, UART_DIVISOR_MSB_REG, divisor >> 8);
            // Clear DLAB
            uartWriteReg (dev,
                          UART_LINE_CTRL_REG,
                          uartReadReg (dev, UART_LINE_CTRL_REG) & ~(UART_LCR_DLAB));
            // Test serial port
            uartWaitForTx (dev);
            uartWriteReg (dev, UART_TXBUF, 0x34);
            if (uartReadReg (dev, UART_RXBUF) != 0x34)
                return false;
            // Test another byte
            uartWaitForTx (dev);
            uartWriteReg (dev, UART_TXBUF, 0x27);
            if (uartReadReg (dev, UART_RXBUF) != 0x27)
                return false;
            // Clear loopback mode
            uartWriteReg (
                dev,
                UART_MODEM_CTRL_REG,
                uartReadReg (dev, UART_MODEM_CTRL_REG) & ~(UART_MCR_LOOPBACK));
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &uart16550SvcTab);
            break;
        }
    }
    return true;
}

static bool Uart16550DumpData (void* objp, void* params)
{
    return true;
}

static bool Uart16550Notify (void* objp, void* params)
{
    // Get notification code
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = params;
    int code = notify->code;
    if (code == NB_SERIAL_NOTIFY_SETOWNER)
    {
        // Notify current owner that we are being deteached
        NbUart16550Dev_t* console = obj->data;
        if (console->owner)
            console->owner->entry (NB_DRIVER_ENTRY_DETACHOBJ, obj);
        NbDriver_t* newDrv = notify->data;
        // Set new owner
        console->owner = newDrv;
        NbObjSetOwner (obj, newDrv);
        // Attach it
        newDrv->entry (NB_DRIVER_ENTRY_ATTACHOBJ, obj);
    }
    return true;
}

static bool Uart16550Write (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbUart16550Dev_t* dev = obj->data;
    uint8_t data = (uint8_t) params;
    // Write it
    uartWaitForTx (dev);
    uartWriteReg (dev, UART_TXBUF, data);
    return true;
}

static bool Uart16550Read (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbUart16550Dev_t* dev = obj->data;
    uint8_t* data = (uint8_t*) params;
    // Read it
    uartWaitForRx (dev);
    *data = uartReadReg (dev, UART_RXBUF);
    return true;
}

NbObjSvc uartSvcs[] = {NULL,
                       NULL,
                       NULL,
                       Uart16550DumpData,
                       Uart16550Notify,
                       Uart16550Write,
                       Uart16550Read};

NbObjSvcTab_t uart16550SvcTab = {ARRAY_SIZE (uartSvcs), uartSvcs};

NbDriver_t uart16550Drv =
    {"Rs232_16550", Uart16550Entry, {0}, 0, false, sizeof (NbUart16550Dev_t)};

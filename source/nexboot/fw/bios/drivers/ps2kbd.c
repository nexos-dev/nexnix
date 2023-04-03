/*
    ps2kbd.c - contains PS/2 keyboard driver
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

#include <assert.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/ps2kbd.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <string.h>

#include "ps2scancodes.h"

// PS/2 port definitions
#define PS2_PORT_STATUS  0x64
#define PS2_PORT_OUTPUT  0x60
#define PS2_PORT_INPUT   0x60
#define PS2_PORT_CONTROL 0x64

// PS/2 port bits
#define PS2_STATUS_OBF     (1 << 0)
#define PS2_STATUS_IBF     (1 << 1)
#define PS2_STATUS_CMDDATA (1 << 3)
#define PS2_STATUS_TXTO    (1 << 5)
#define PS2_STATUS_TO      (1 << 6)

// PS/2 controller commands
#define PS2_COMMAND_DISABLE_KBD 0xAD
#define PS2_COMMAND_ENABLE_KBD  0xAE
#define PS2_COMMAND_READ_CCB    0x20
#define PS2_COMMAND_WRITE_CCB   0x60

// PS/2 keyboard commands
#define PS2_COMMAND_RESET               0xFF
#define PS2_COMMAND_START_SCANNING      0xF4
#define PS2_COMMAND_SET_KEYS_NORMAL     0xFA
#define PS2_COMMAND_SET_KEYS_MAKE       0xF9
#define PS2_COMMAND_SET_KEYS_MAKE_BREAK 0xF8
#define PS2_COMMAND_SET_KEYS_TYPEMATIC  0xF7
#define PS2_COMMAND_SET_DEFAULTS        0xF6
#define PS2_COMMAND_READ_ID             0xF2
#define PS2_COMMAND_SET_LEDS            0xED

// PS/2 ID bytes
#define PS2_ID_BYTE1 0xAB
#define PS2_ID_BYTE2 0x83

// PS/2 LED bits
#define PS2_LED_SCROLL_LOCK (1 << 0)
#define PS2_LED_NUM_LOCK    (1 << 1)
#define PS2_LED_CAPS_LOCK   (1 << 2)

// PS/2 command responses
#define PS2_RESULT_ACK    0xFA
#define PS2_RESULT_ECHO   0xEE
#define PS2_RESULT_RESEND 0xFE

// PS/2 CCB bits
#define PS2_CCB_INTS    (1 << 0)
#define PS2_CCB_XLAT    (1 << 6)
#define PS2_CCB_DISABLE (1 << 4)
#define PS2_CCB_PC      (1 << 5)

extern NbObjSvcTab_t ps2KbdSvcTab;

static bool kbdFound = false;

// Waits for input buffer to be empty
static void ps2WaitInputBuf()
{
    uint8_t status = NbInb (PS2_PORT_STATUS);
    while (status & PS2_STATUS_IBF)
        status = NbInb (PS2_PORT_STATUS);
}

// Waits for output buffer to be full
static void ps2WaitOutputBuf()
{
    uint8_t status = NbInb (PS2_PORT_STATUS);
    while (!(status & PS2_STATUS_OBF))
        status = NbInb (PS2_PORT_STATUS);
}

// Sends command to keyboard controller
static void ps2SendCtrlCmd (uint8_t cmd)
{
    ps2WaitInputBuf();
    NbOutb (PS2_PORT_CONTROL, cmd);    // Send command
}

// Sends command to keyboard controller with parameter
static void ps2SendCtrlCmdParam (uint8_t cmd, uint8_t param)
{
    ps2WaitInputBuf();
    NbOutb (PS2_PORT_CONTROL, cmd);
    ps2WaitInputBuf();
    NbOutb (PS2_PORT_OUTPUT, param);
}

// Reads data from keyboard buffer
static uint8_t ps2ReadData()
{
    ps2WaitOutputBuf();
    return NbInb (PS2_PORT_INPUT);
}

// Sends command to keyboard
static void ps2SendKbdCmd (uint8_t cmd)
{
    ps2WaitInputBuf();
    NbOutb (PS2_PORT_OUTPUT, cmd);
}

// Sends command to keyboard with parameter
static void ps2SendKbdCmdParam (uint8_t cmd, uint8_t param)
{
    ps2WaitInputBuf();
    NbOutb (PS2_PORT_OUTPUT, cmd);
    ps2WaitInputBuf();
    NbOutb (PS2_PORT_OUTPUT, param);
}

// Sets an LED
static void ps2ToggleLed (NbPs2Kbd_t* drv, int led)
{
    if (!(drv->ledFlags & led))
    {
        drv->ledFlags | led;
        ps2SendKbdCmdParam (PS2_COMMAND_SET_LEDS, drv->ledFlags);
        assert (ps2ReadData() == PS2_RESULT_ACK);
    }
    else
    {
        drv->ledFlags &= ~led;
        ps2SendKbdCmdParam (PS2_COMMAND_SET_LEDS, drv->ledFlags);
        assert (ps2ReadData() == PS2_RESULT_ACK);
    }
}

static bool Ps2KbdEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_DETECTHW: {
            if (kbdFound)
                return false;
            while (NbInb (PS2_PORT_STATUS) & PS2_STATUS_OBF)
                ps2ReadData();    // Read data to flush output buffer
            // Disable translation, interupts, and put in PC mode
            ps2SendCtrlCmd (PS2_COMMAND_READ_CCB);
            uint8_t ccb = ps2ReadData();
            ccb &= ~PS2_CCB_INTS;
            ccb &= ~PS2_CCB_XLAT;
            ccb |= PS2_CCB_PC;
            ps2SendCtrlCmdParam (PS2_COMMAND_WRITE_CCB, ccb);
            ps2SendCtrlCmd (PS2_COMMAND_ENABLE_KBD);
            // Detect if a keyboard exists. To do this, we send the read ID
            // command If we get a timeout error when sending it, a keyboard
            // isn't plugged in
            ps2SendKbdCmd (PS2_COMMAND_READ_ID);
            // Read status port, and check for OBF and TXTO
            uint8_t status = NbInb (PS2_PORT_STATUS);
            while (1)
            {
                if (status & PS2_STATUS_OBF)
                    break;
                // Check TO and TXTO
                if (status & PS2_STATUS_TXTO || status & PS2_STATUS_TO)
                {
                    // No keyboard exists
                    NbLogMessageEarly ("PS/2 Keyboard not found",
                                       NEXBOOT_LOGLEVEL_INFO);
                    return false;
                }
            }
            // Check ID
            if (ps2ReadData() != PS2_RESULT_ACK || ps2ReadData() != PS2_ID_BYTE1 ||
                ps2ReadData() != PS2_ID_BYTE2)
                return false;    // No keyboard
            // Set up keyboard parameters
            ps2SendKbdCmd (PS2_COMMAND_SET_DEFAULTS);
            assert (ps2ReadData() == PS2_RESULT_ACK);
            ps2SendKbdCmd (PS2_COMMAND_START_SCANNING);
            assert (ps2ReadData() == PS2_RESULT_ACK);
            // Prepare device structure
            NbPs2Kbd_t* ps2Dev = params;
            ps2Dev->hdr.devSubType = NB_DEVICE_SUBTYPE_PS2KBD;
            ps2Dev->hdr.devId = 0;
            ps2Dev->owner = NULL;
            ps2Dev->capsState = false;
            kbdFound = true;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &ps2KbdSvcTab);
            break;
        }
    }
    return true;
}

static bool Ps2DumpData (void* objp, void* params)
{
    return true;
}

static bool Ps2Notify (void* objp, void* params)
{
    // Get notification code
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = params;
    int code = notify->code;
    if (code == NB_KEYBOARD_NOTIFY_SETOWNER)
    {
        // Notify current owner that we are being deteached
        NbPs2Kbd_t* console = obj->data;
        if (console->owner)
            console->owner->entry (NB_DRIVER_ENTRY_DETACHOBJ, obj);
        NbDriver_t* newDrv = notify->data;
        // Set new owner
        console->owner = newDrv;
        // Attach it
        newDrv->entry (NB_DRIVER_ENTRY_ATTACHOBJ, obj);
    }
    return true;
}

static bool Ps2ReadKey (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbPs2Kbd_t* kbd = obj->data;
    NbKeyData_t* keyData = params;
    // Check if were in caps mode
    if (kbd->capsState)
        keyData->flags |= NB_KEY_FLAG_CAPS;
    if (kbd->shiftState)
        keyData->flags |= NB_KEY_FLAG_SHIFT;
    if (kbd->altState)
        keyData->flags |= NB_KEY_FLAG_ALT;
    if (kbd->ctrlState)
        keyData->flags |= NB_KEY_FLAG_CTRL;
    // So, here's our logic: we read data from the keyboard in a sort of state
    // machine
    bool isBreak = false;
    bool isExtCode = false;
    bool isFinished = false;
    while (!isFinished)
    {
        uint8_t scanCode = ps2ReadData();
        if (scanCode == 0xF0)
        {
            isBreak = true;
            continue;
        }
        else if (scanCode == 0xE0)
        {
            isExtCode = true;
            continue;
        }
        // Get character
        uint8_t c = 0;
        if (isExtCode)
            c = scanToEnUs2[scanCode];
        else
            c = scanToEnUs[scanCode];
        // Defer caps lock handling until break code
        if (c == PS2_KEY_CAPS_LOCK)
        {
            if (isBreak)
            {
                if (kbd->capsState)
                {
                    // Turn off caps flags
                    kbd->capsState = false;
                    keyData->flags &= ~NB_KEY_FLAG_CAPS;
                }
                else
                {
                    // Turn on caps flag
                    kbd->capsState = true;
                    keyData->flags |= NB_KEY_FLAG_CAPS;
                }
                // Toggle LED
                ps2ToggleLed (kbd, PS2_LED_CAPS_LOCK);
                isBreak = false;
            }
            if (isExtCode)
                isExtCode = false;
            continue;
        }
        // Handle num lock
        else if (c == PS2_KEY_NUM_LOCK)
        {
            if (isBreak)
                ps2ToggleLed (kbd, PS2_LED_NUM_LOCK);
            if (isBreak)
                isBreak = false;
            if (isExtCode)
                isExtCode = false;
            continue;
        }
        // Handle shift
        else if (c == PS2_KEY_SHIFT)
        {
            // Turn flag on or off
            if (isBreak)
            {
                keyData->flags &= ~NB_KEY_FLAG_SHIFT;
                kbd->shiftState = false;
            }
            else
            {
                keyData->flags |= NB_KEY_FLAG_SHIFT;
                kbd->shiftState = true;
            }
            if (isBreak)
                isBreak = false;
            if (isExtCode)
                isExtCode = false;
            continue;
        }
        // Handle CTRL and ALT
        else if (c == PS2_KEY_CTRL)
        {
            if (isBreak)
            {
                keyData->flags &= ~NB_KEY_FLAG_CTRL;
                kbd->ctrlState = false;
            }
            else
            {
                keyData->flags |= NB_KEY_FLAG_CTRL;
                kbd->ctrlState = true;
            }
            if (isBreak)
                isBreak = false;
            if (isExtCode)
                isExtCode = false;
            continue;
        }
        else if (c == PS2_KEY_ALT)
        {
            if (isBreak)
            {
                keyData->flags &= ~NB_KEY_FLAG_ALT;
                kbd->altState = false;
            }
            else
            {
                keyData->flags |= NB_KEY_FLAG_ALT;
                kbd->altState = true;
            }
            if (isBreak)
                isBreak = false;
            if (isExtCode)
                isExtCode = false;
            continue;
        }
        // Check if shift is held. The boolean condition ensures that if caps lock is
        // held too then we don't capitalize, but still in that case handle
        // non-alphabetic shifts
        if (kbd->shiftState && (!kbd->capsState || !(c >= 'a' && c <= 'z')))
            c = scanToEnUsUppercase[scanCode];
        else if (kbd->capsState && !kbd->shiftState)
        {
            // Determine if this is a letter or not
            if (c >= 'a' && c <= 'z')
                c -= 32;    // Convert to a capital letter
        }
        keyData->isBreak = isBreak;
        keyData->c = c;
        // Maybe this is an escape code
        if (c >= 0xF0 && c <= PS2_KEY_END)
        {
            // Convert to escape code
            keyData->isEscCode = true;
            keyData->escCode = keyToEscCode[c - 0xF1];
        }
        if (isExtCode)
            isExtCode = false;
        isFinished = true;
    }
    return true;
}

static NbObjSvc ps2KbdSvcs[] =
    {NULL, NULL, NULL, Ps2DumpData, Ps2Notify, Ps2ReadKey};

NbObjSvcTab_t ps2KbdSvcTab = {ARRAY_SIZE (ps2KbdSvcs), ps2KbdSvcs};

NbDriver_t ps2KbdDrv = {"PS2Kbd", Ps2KbdEntry, {0}, 0, false, sizeof (NbPs2Kbd_t)};

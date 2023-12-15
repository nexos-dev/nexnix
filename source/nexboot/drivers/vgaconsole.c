/*
    vgaconsole.c - contains VGA text console driver
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
#include <nexboot/drivers/vgaconsole.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <string.h>

extern NbObjSvcTab_t vgaSvcTab;
extern NbDriver_t vgaConsoleDrv;

// This driver always runs (currently) in mode 03h, which 80x25, with 16 colors
// Note that in this mode, the buffer starts at 0xB8000, and operates in planar mode
// with odd / even addressing, meaning that byte 0 corresponds to plane 0, byte 1 to
// plane 1, byte 2 to plane 0, and so on. Plane 2 is only accesible via hacks, plane
// 3 is unused. Plane 0 contains color attributes, plane 1 contains ASCII codes, and
// plane 2 contains font data. Note that ASCII escape sequences are not interpreted,
// it prints stuff as it is passed. The terminal layer driver must interpret escape
// sequences and accordingly adjust the associated line

// VGA memory access macros
#define VGA_MEMBASE             0xB8000
#define VGA_MAKECOLOR(bg, fg)   (((bg) << 4) | (fg))
#define VGA_MAKEENTRY(c, color) ((c) | ((color) << 8))

// CRTC macros
#define VGA_CRTC_INDEX              0x3D4
#define VGA_CRTC_DATA               0x3D5
#define VGA_CRTC_INDEX_CURSOR_START 0x0A
#define VGA_CRTC_INDEX_CURSOR_END   0x0B
#define VGA_CRTC_INDEX_CURSOR_HIGH  0x0E
#define VGA_CRTC_INDEX_CURSOR_LOW   0x0F

// Driver main entry point
static bool VgaConsoleEntry (int code, void* params)
{
    if (code == NB_DRIVER_ENTRY_DETECTHW)
    {
        // Set up basic hardware stuff
        NbVgaConsole_t* consoleData = params;
        consoleData->hdr.devId = 0;
        consoleData->hdr.devSubType = NB_DEVICE_SUBTYPE_VGACONSOLE;
        consoleData->hdr.sz = sizeof (NbVgaConsole_t);
        consoleData->cols = NB_VGA_CONSOLE_03H_COLS;
        consoleData->rows = NB_VGA_CONSOLE_03H_ROWS;
        consoleData->mode = NB_VGA_CONSOLE_MODE_03H;
        consoleData->bgColor = vgaColorTab[NB_CONSOLE_COLOR_BLACK];
        consoleData->fgColor = vgaColorTab[NB_CONSOLE_COLOR_WHITE];
    }
    else if (code == NB_DRIVER_ENTRY_ATTACHOBJ)
    {
        // Set the interface
        NbObject_t* obj = params;
        NbObjInstallSvcs (obj, &vgaSvcTab);
        NbObjSetManager (obj, &vgaConsoleDrv);
    }
    return true;
}

// Writes a character to display at X-Y location
static void vgaWriteChar (NbVgaConsole_t* console,
                          char c,
                          int bgColor,
                          int fgColor,
                          int x,
                          int y)
{
    uint16_t* textBuf = (uint16_t*) VGA_MEMBASE;
    int location = (y * console->cols) + x;
    // Write it out
    textBuf[location] =
        VGA_MAKEENTRY (c, VGA_MAKECOLOR (console->bgColor, console->fgColor));
}

// Moves the text cursor to X-Y location
static void vgaMoveCursor (NbVgaConsole_t* console, int x, int y)
{
    uint16_t location = (y * console->cols) + x;
    NbOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_HIGH);
    NbIoWait();
    NbOutb (VGA_CRTC_DATA, location >> 8);
    NbIoWait();
    NbOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_LOW);
    NbIoWait();
    NbOutb (VGA_CRTC_DATA, location & 0xFF);
}

// Object services
static bool VgaObjDestroy (void* objp, void* data)
{
    NbObject_t* obj = objp;
    free (obj->data);
    return true;
}

static bool VgaObjDumpData (void* obj, void* data)
{
    NbObject_t* vgaObj = obj;
    NbVgaConsole_t* vga = NbObjGetData (vgaObj);
    void (*writeData) (const char* fmt, ...) = data;
    if (vga->owner)
        writeData ("Owner driver: %s", vga->owner->name);
    writeData ("Number of columns: %d\n", vga->cols);
    writeData ("Number of rows: %d\n", vga->rows);
    return true;
}

static bool VgaClearScreen (void* objp, void* unused)
{
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    for (int i = 0; i < console->rows; ++i)
    {
        for (int j = 0; j < console->cols; ++j)
            vgaWriteChar (console, ' ', console->bgColor, console->fgColor, j, i);
    }
    vgaMoveCursor (console, 0, 0);
    return true;
}

static bool VgaObjInit (void* obj, void* data)
{
    // VgaClearScreen (obj, NULL);
    return true;
}

static bool VgaObjNotify (void* objp, void* data)
{
    // Get notification code
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = data;
    int code = notify->code;
    if (code == NB_CONSOLE_NOTIFY_SETOWNER)
    {
        // Notify current owner that we are being deteached
        NbVgaConsole_t* console = obj->data;
        if (console->owner)
            console->owner->entry (NB_DRIVER_ENTRY_DETACHOBJ, obj);
        NbDriver_t* newDrv = notify->data;
        // Set new owner
        console->owner = newDrv;
        NbObjSetOwner (obj, newDrv);
    }
    return true;
}

static bool VgaPutChar (void* objp, void* data)
{
    assert (data);
    NbPrintChar_t* charData = data;
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    vgaWriteChar (console,
                  charData->c,
                  console->bgColor,
                  console->fgColor,
                  charData->col,
                  charData->row);
    vgaMoveCursor (console, charData->col + 1, charData->row);
    return true;
}

static bool VgaDisableCursor (void* objp, void* unused)
{
    NbOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_START);
    NbIoWait();
    NbOutb (VGA_CRTC_DATA, (1 << 5));
    NbIoWait();
    return true;
}

static bool VgaEnableCursor (void* objp, void* unused)
{
    NbOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_START);
    NbIoWait();
    NbOutb (VGA_CRTC_DATA, 13);
    NbIoWait();
    NbOutb (VGA_CRTC_INDEX, VGA_CRTC_INDEX_CURSOR_END);
    NbIoWait();
    NbOutb (VGA_CRTC_DATA, 14);
    NbIoWait();
    return true;
}

static bool VgaSetFgColor (void* objp, void* colorp)
{
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    int color = (int) colorp;
    color = vgaColorTab[color];
    console->fgColor = color;
    return true;
}

static bool VgaSetBgColor (void* objp, void* colorp)
{
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    int color = (int) colorp;
    color = vgaColorTab[color];
    console->bgColor = color;
    return true;
}

static bool VgaScrollDown (void* objp, void* unused)
{
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    uint16_t* vgaBase = (uint16_t*) VGA_MEMBASE;
    // Copy rows backwards
    for (int i = 0; i < (console->rows - 1); ++i)
    {
        memcpy (vgaBase, vgaBase + console->cols, console->cols * 2);
        vgaBase += console->cols;
    }
    // Write out spaces
    for (int i = 0; i < console->cols; ++i)
        vgaWriteChar (console,
                      ' ',
                      console->bgColor,
                      console->fgColor,
                      i,
                      console->rows - 1);
    return true;
}

static bool VgaMoveCursor (void* objp, void* data)
{
    assert (data);
    NbConsoleLoc_t* loc = data;
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    vgaMoveCursor (console, loc->col, loc->row);
    return true;
}

static bool VgaGetSize (void* objp, void* data)
{
    assert (objp && data);
    NbConsoleSz_t* out = data;
    NbObject_t* obj = objp;
    NbVgaConsole_t* console = obj->data;
    out->cols = console->cols;
    out->rows = console->rows;
    return true;
}

// Object interface
static NbObjSvc vgaServices[] = {VgaObjInit,
                                 NULL,
                                 VgaObjDestroy,
                                 VgaObjDumpData,
                                 VgaObjNotify,
                                 VgaClearScreen,
                                 VgaPutChar,
                                 VgaDisableCursor,
                                 VgaEnableCursor,
                                 VgaSetFgColor,
                                 VgaSetBgColor,
                                 VgaScrollDown,
                                 VgaMoveCursor,
                                 VgaGetSize};

NbObjSvcTab_t vgaSvcTab = {ARRAY_SIZE (vgaServices), vgaServices};

// Driver structure
NbDriver_t vgaConsoleDrv =
    {"VgaConsole", VgaConsoleEntry, {0}, 0, false, sizeof (NbVgaConsole_t)};

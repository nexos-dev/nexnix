/*
    fbconsole.c - contains framebuffer console driver
    Copyright 2023 - 2024 The NexNix Project

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

//#include "Tamsyn8x16r.h"
#include "font-8x16.h"
#include <assert.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/display.h>
#include <nexboot/drivers/terminal.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <stdio.h>
#include <string.h>

extern NbObjSvcTab_t fbConsSvcTab;
extern NbDriver_t fbConsDrv;

// Line structure
typedef struct _fbConsLine
{
    int row;                     // Row line is at in cells
    int width;                   // Width of line in cells
    struct _fbConsLine* next;    // Pointer to next one
} NbFbConsLine_t;

// fbconsole structure
typedef struct _fbconsole
{
    NbObject_t* display;    // Display we are on
    bool cursorEnabled;     // Is cursor enabled?
    int cursorX;            // Cursor X
    int cursorY;            // Cursor Y
    void* font;             // Font base
    int rows;               // Size of screen
    int cols;
    int fgColor;    // Color info
    int bgColor;
    int charHeight;    // Height of  a character
    int charWidth;     // Width of a character
    int fontCharSz;
    int lastRow;    // Last row character was printed on
    int lastCol;
    NbFbConsLine_t* lineList;    // List of line used for scrolling
    NbFbConsLine_t* lineListEnd;
} NbFbCons_t;

static NbFbCons_t* consoles[32] = {0};
static int curCons = 0;

static uint32_t colorTab32[] = {0, 0xFF0000, 0xFF00, 0xFFFF00, 0xFF, 0xFF00FF, 0xFFFF, 0xd3d3d3};
static uint16_t colorTab16[] = {0, 0xF800, 0x7E0, 0xFFE0, 0x1F, 0xF81F, 0x7FF, 0xD6DB};

// fbconsole entry
static bool FbConsDrvEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_START: {
            // Find a display object
            NbObject_t* iter = NULL;
            NbObject_t* devs = NbObjFind ("/Devices");
            NbObject_t* display = NULL;
            while ((iter = NbObjEnumDir (devs, iter)))
            {
                if (iter->type == OBJ_TYPE_DEVICE && iter->interface == OBJ_INTERFACE_DISPLAY &&
                    !iter->owner)
                {
                    display = iter;
                    break;
                }
            }
            // If no display was found, return
            if (!display)
                return true;
            // Initialize structure
            char buf[64] = {0};
            snprintf (buf, 64, "/Devices/FbConsole%d", curCons);
            NbObject_t* consObj = NbObjCreate (buf, OBJ_TYPE_DEVICE, OBJ_INTERFACE_CONSOLE);
            NbFbCons_t* cons = (NbFbCons_t*) calloc (1, sizeof (NbFbCons_t));
            if (!cons || !consObj)
                return false;
            consoles[curCons] = cons;
            ++curCons;
            cons->display = NbObjRef (display);
            cons->bgColor = NB_CONSOLE_COLOR_BLACK;
            cons->fgColor = NB_CONSOLE_COLOR_WHITE;
            cons->charHeight = 16;
            cons->charWidth = 8;
            cons->fontCharSz = 16;
            // Get display size
            NbDisplayDev_t* displaySt = NbObjGetData (display);
            cons->cols = displaySt->width / cons->charWidth;
            cons->rows = displaySt->height / cons->charHeight;
            cons->font = &fb_font;
            // cons->cursorEnabled = true;
            //   Setup object
            NbObjSetData (consObj, cons);
            NbObjSetManager (consObj, &fbConsDrv);
            NbObjInstallSvcs (consObj, &fbConsSvcTab);
            // Set ownership
            NbObjNotify_t notify;
            notify.code = NB_DISPLAY_NOTIFY_SETOWNER;
            notify.data = &fbConsDrv;
            NbObjCallSvc (display, OBJ_SERVICE_NOTIFY, &notify);
            break;
        }
        case NB_DISPLAY_CODE_SETMODE: {
            NbDisplayDev_t* displaySt = params;
            // Find console for device
            NbFbCons_t* cons = NULL;
            for (int i = 0; i < curCons; ++i)
            {
                if (consoles[i]->display->data == displaySt)
                {
                    cons = consoles[i];
                    break;
                }
            }
            if (!cons)
                return false;
            cons->bgColor = NB_CONSOLE_COLOR_BLACK;
            cons->fgColor = NB_CONSOLE_COLOR_WHITE;
            // Get display size
            cons->cols = displaySt->width / cons->charWidth;
            cons->rows = displaySt->height / cons->charHeight;
            // Notify terminal
            NbObjNotify_t notify;
            notify.code = NB_TERMINAL_NOTIFY_RESIZE;
            NbTermResize_t resize;
            resize.console = cons;
            resize.sz.cols = cons->cols;
            resize.sz.rows = cons->rows;
            notify.data = &resize;
            NbSendDriverCode (NbFindDriver ("Terminal"), NB_TERMINAL_NOTIFY_RESIZE, &notify);
            break;
        }
    }
    return true;
}

static void fbMoveCursor (NbFbCons_t* console, int col, int row)
{
    if (!console->cursorEnabled)
        return;
    NbDisplayDev_t* display = NbObjGetData (console->display);
    // Determine where to place cursor
    int cursorX = (col * console->charWidth) + 1;
    int cursorY = (row * console->charHeight) + (console->charHeight - 2);
    int cursorWidth = 8;
    int cursorHeight = 2;
    void* buf = display->backBufferLoc + (cursorY * display->bytesPerLine) +
                (cursorX * display->bytesPerPx);
    void* bufEnd = display->backBuffer + display->lfbSize;
    // Wrap if needed
    if (buf >= bufEnd)
    {
        uint32_t diff = buf - bufEnd;
        buf = display->backBuffer + diff;
    }
    for (int y = 0; y < cursorHeight; ++y)
    {
        for (int x = 0; x < cursorWidth; ++x)
        {
            if (display->bpp == 32)
                DISPLAY_PLOT_32BPP (display, buf, colorTab32[console->fgColor], x, 0);
            if (display->bpp == 16)
                DISPLAY_PLOT_16BPP (display, buf, colorTab16[console->fgColor], x, 0);
        }
        buf += display->bytesPerLine;
        // Wrap if needed
        if (buf >= bufEnd)
        {
            uint32_t diff = buf - bufEnd;
            buf = display->backBuffer + diff;
        }
    }
    // Invalidate it
    NbInvalidRegion_t region;
    region.startX = cursorX;
    region.startY = cursorY;
    region.width = cursorWidth;
    region.height = cursorHeight;
    NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &region);
    // Overwrite last cursor
    if (console->cursorX && console->cursorY)
    {
        buf = display->backBufferLoc + (console->cursorY * display->bytesPerLine) +
              (console->cursorX * display->bytesPerPx);
        bufEnd = display->backBuffer + display->lfbSize;
        // Wrap if needed
        if (buf >= bufEnd)
        {
            uint32_t diff = buf - bufEnd;
            buf = display->backBuffer + diff;
        }
        for (int y = 0; y < cursorHeight; ++y)
        {
            for (int x = 0; x < cursorWidth; ++x)
            {
                if (display->bpp == 32)
                    DISPLAY_PLOT_32BPP (display, buf, colorTab32[console->bgColor], x, 0);
                if (display->bpp == 16)
                    DISPLAY_PLOT_16BPP (display, buf, colorTab16[console->bgColor], x, 0);
            }
            buf += display->bytesPerLine;
            // Wrap if needed
            if (buf >= bufEnd)
            {
                uint32_t diff = buf - bufEnd;
                buf = display->backBuffer + diff;
            }
        }
        // Invalidate it
        region.startX = console->cursorX;
        region.startY = console->cursorY;
        region.width = cursorWidth;
        region.height = cursorHeight;
        NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &region);
    }
    // Update cursor location
    console->cursorX = cursorX;
    console->cursorY = cursorY;
}

static bool FbObjDumpData (void* objp, void* params)
{
    NbObject_t* obj = objp;
    return true;
}

static bool FbObjNotify (void* objp, void* data)
{
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = data;
    int code = notify->code;
    if (code == NB_CONSOLE_NOTIFY_SETOWNER)
    {
        // Notify current owner that we are being deteached
        if (obj->owner)
            obj->owner->entry (NB_DRIVER_ENTRY_DETACHOBJ, obj);
        NbDriver_t* newDrv = notify->data;
        // Set new owner
        NbObjSetOwner (obj, newDrv);
    }
    return true;
}

static bool FbObjClearScreen (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* console = NbObjGetData (obj);
    NbDisplayDev_t* display = NbObjGetData (console->display);
    // Overwrite screen
    display->backBufferLoc = display->backBuffer;
    void* buf = display->backBuffer;
    for (int i = 0; i < display->height; ++i)
    {
        for (int j = 0; j < display->width; ++j)
        {
            if (display->bpp == 32)
                DISPLAY_PLOT_32BPP (display, buf, colorTab32[console->bgColor], j, 0);
            if (display->bpp == 16)
                DISPLAY_PLOT_16BPP (display, buf, colorTab16[console->bgColor], j, 0);
        }
        buf += display->bytesPerLine;
    }
    NbInvalidRegion_t region;
    region.startX = 0;
    region.startY = 0;
    region.height = display->height;
    region.width = display->width;
    NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &region);
    return true;
}

static bool FbObjPutChar (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbPrintChar_t* pc = params;
    NbFbCons_t* console = NbObjGetData (obj);
    NbDisplayDev_t* display = NbObjGetData (console->display);
    // Get color info
    uint32_t pxColor = 0;
    if (display->bpp == 32)
    {
        uint32_t pxColorTmp = colorTab32[console->fgColor];
        uint8_t r, g, b;
        DISPLAY_DECOMPOSE_RGB (pxColorTmp, r, g, b);
        pxColor = DISPLAY_COMPOSE_RGB (display, r, g, b);
    }
    else if (display->bpp == 16)
    {
        uint16_t pxColorTmp = colorTab16[console->fgColor];
        uint8_t r, g, b;
        DISPLAY_DECOMPOSE_RGB16 (pxColorTmp, r, g, b);
        pxColor = DISPLAY_COMPOSE_RGB (display, r, g, b);
    }
    uint32_t bgPxColor = 0;
    if (display->bpp == 32)
    {
        uint32_t pxColorTmp = colorTab32[console->bgColor];
        uint8_t r, g, b;
        DISPLAY_DECOMPOSE_RGB (pxColorTmp, r, g, b);
        bgPxColor = DISPLAY_COMPOSE_RGB (display, r, g, b);
    }
    else if (display->bpp == 16)
    {
        uint16_t pxColorTmp = colorTab16[console->bgColor];
        uint8_t r, g, b;
        DISPLAY_DECOMPOSE_RGB16 (pxColorTmp, r, g, b);
        bgPxColor = DISPLAY_COMPOSE_RGB (display, r, g, b);
    }
    // Compute glyph
    bool found;
    uint16_t glyphIdx = fb_font_glyph (pc->c, &found);
    // Grab character from font table
    uint8_t* glyph = console->font + (glyphIdx * console->fontCharSz);
    uint32_t glyphRowSz = (console->charWidth + 7) / 8;
    // Compute base offset to character
    uint32_t offset = (pc->row * console->charHeight * display->bytesPerLine) +
                      (pc->col * console->charWidth * display->bytesPerPx);
    // Get base of buffer
    void* buf = display->backBufferLoc + offset;
    void* bufEnd = display->backBuffer + display->lfbSize;
    // Wrap if needed
    if (buf >= bufEnd)
    {
        uint32_t diff = buf - bufEnd;
        buf = display->backBuffer + diff;
    }
    // Display pixels
    uint32_t mask = 1 << console->charWidth;
    for (int y = 0; y < console->charHeight; ++y)
    {
        void* lineBuf = buf;
        uint32_t omask = mask;
        for (int x = 0; x < (console->charWidth + 1); ++x)
        {
            if (display->bytesPerPx == 2)
            {
                if ((*glyph) & omask)
                    DISPLAY_PLOT_16BPP (display, lineBuf, pxColor, 0, 0);
                else
                    DISPLAY_PLOT_16BPP (display, lineBuf, bgPxColor, 0, 0);
                lineBuf += 2;
            }
            else if (display->bytesPerPx == 4)
            {
                if ((*glyph) & omask)
                    DISPLAY_PLOT_32BPP (display, lineBuf, pxColor, 0, 0);
                else
                    DISPLAY_PLOT_32BPP (display, lineBuf, bgPxColor, 0, 0);
                lineBuf += 4;
            }
            omask >>= 1;    // Wrap if needed
            if (lineBuf >= bufEnd)
            {
                uint32_t diff = lineBuf - bufEnd;
                lineBuf = display->backBuffer + diff;
            }
        }
        buf += display->bytesPerLine;
        if (buf >= bufEnd)
        {
            uint32_t diff = buf - bufEnd;
            buf = display->backBuffer + diff;
        }
        glyph += glyphRowSz;
    }
    // Invalidate region
    NbInvalidRegion_t region;
    region.height = console->charHeight;
    region.width = console->charWidth;
    region.startY = pc->row * console->charHeight;
    region.startX = pc->col * console->charWidth;
    NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &region);
    // Move cursor
    fbMoveCursor (console, pc->col + 1, pc->row);
    // Set last row and column
    console->lastRow = pc->row;
    console->lastCol = pc->col;
    return true;
}

static bool FbObjDisableCursor (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* console = NbObjGetData (obj);
    console->cursorEnabled = false;
    // Overwrite cursor
    NbDisplayDev_t* display = NbObjGetData (console->display);
    int cursorWidth = 8;
    int cursorHeight = 2;
    void* buf = display->backBufferLoc + (console->cursorY * display->bytesPerLine) +
                (console->cursorX * display->bytesPerPx);
    for (int y = 0; y < cursorHeight; ++y)
    {
        for (int x = 0; x < cursorWidth; ++x)
        {
            if (display->bpp == 32)
                DISPLAY_PLOT_32BPP (display, buf, colorTab32[console->bgColor], x, 0);
            if (display->bpp == 16)
                DISPLAY_PLOT_16BPP (display, buf, colorTab16[console->bgColor], x, 0);
        }
        buf += display->bytesPerLine;
    }
    // Invalidate it
    NbInvalidRegion_t region;
    region.startX = console->cursorX;
    region.startY = console->cursorY;
    region.width = cursorWidth;
    region.height = cursorHeight;
    NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &region);
    return true;
}

static bool FbObjEnableCursor (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* console = NbObjGetData (obj);
    // console->cursorEnabled = true;
    return true;
}

static bool FbObjSetFgColor (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* dev = NbObjGetData (obj);
    int color = (int) params;
    dev->fgColor = color;
    return true;
}

static bool FbObjSetBgColor (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* dev = NbObjGetData (obj);
    int color = (int) params;
    dev->bgColor = color;
    return true;
}

static bool FbObjScroll (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* console = NbObjGetData (obj);
    NbDisplayDev_t* dev = NbObjGetData (console->display);
    // Increment current back buffer base
    for (int i = 0; i < console->charHeight; ++i)
        NbObjCallSvc (console->display, NB_DISPLAY_INCRENDER, NULL);
    // Invalidate
    NbInvalidRegion_t scrollRegion;
    scrollRegion.startX = 0;
    scrollRegion.startY = 0;
    scrollRegion.width = dev->width;
    scrollRegion.height = (console->rows - 1) * console->charHeight;
    NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &scrollRegion);
    // Clear last line
    NbDisplayDev_t* display = NbObjGetData (console->display);
    void* lastLineBuf = display->backBufferLoc +
                        (display->bytesPerLine * ((console->rows - 1) * console->charHeight));
    void* bufEnd = display->backBuffer + display->lfbSize;
    // Wrap if needed
    if (lastLineBuf >= bufEnd)
    {
        uint32_t diff = lastLineBuf - bufEnd;
        lastLineBuf = display->backBuffer + diff;
    }
    for (int y = 0; y < console->charHeight; ++y)
    {
        for (int x = 0; x < display->width; ++x)
        {
            if (display->bpp == 32)
                DISPLAY_PLOT_32BPP (display, lastLineBuf, colorTab32[console->bgColor], x, 0);
            if (display->bpp == 16)
                DISPLAY_PLOT_16BPP (display, lastLineBuf, colorTab16[console->bgColor], x, 0);
        }
        lastLineBuf += display->bytesPerLine;
        // Wrap if needed
        if (lastLineBuf >= bufEnd)
        {
            uint32_t diff = lastLineBuf - bufEnd;
            lastLineBuf = display->backBuffer + diff;
        }
    }
    NbInvalidRegion_t region;
    region.startX = 0;
    region.startY = ((console->rows - 1) * console->charHeight);
    region.width = display->width;
    region.height = console->charHeight;
    NbObjCallSvc (console->display, NB_DISPLAY_INVALIDATE, &region);
    return true;
}

static bool FbObjMoveCursor (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbConsoleLoc_t* cursorLoc = params;
    NbFbCons_t* console = NbObjGetData (obj);
    fbMoveCursor (console, cursorLoc->col, cursorLoc->row);
    return true;
}

static bool FbObjGetSize (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbFbCons_t* dev = NbObjGetData (obj);
    NbConsoleSz_t* sz = params;
    sz->cols = dev->cols;
    sz->rows = dev->rows;
    return true;
}

static NbObjSvc fbConsSvcs[] = {NULL,
                                NULL,
                                NULL,
                                FbObjDumpData,
                                FbObjNotify,
                                FbObjClearScreen,
                                FbObjPutChar,
                                FbObjDisableCursor,
                                FbObjEnableCursor,
                                FbObjSetFgColor,
                                FbObjSetBgColor,
                                FbObjScroll,
                                FbObjMoveCursor,
                                FbObjGetSize};

NbObjSvcTab_t fbConsSvcTab = {.numSvcs = ARRAY_SIZE (fbConsSvcs), .svcTab = fbConsSvcs};
NbDriver_t fbConsDrv = {.deps = {0},
                        .devSize = 0,
                        .entry = FbConsDrvEntry,
                        .name = "FbConsole",
                        .numDeps = 0,
                        .started = false};

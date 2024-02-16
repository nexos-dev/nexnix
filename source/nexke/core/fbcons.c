/*
    fbcons.c - contains framebuffer console driver
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

#include "font-8x16.h"
#include <assert.h>
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <string.h>

// Display structure
static NexNixDisplay_t* display = NULL;

// Current column and row
static int curCol = 0, curRow = 0;

// Max row / col
static int rows = 0, cols = 0;

static void* font = &fb_font;

// Color macros
#define COLOR_BLACK   0
#define COLOR_WHITE32 0xD3D3D3
#define COLOR_WHITE16 0xD6DB

// Display manipulation macros
#define DISPLAY_DECOMPOSE_RGB(rgb, r, g, b) \
    ((r) = (((rgb) & (0xFF0000)) >> 16), (g) = (((rgb) & (0xFF00)) >> 8), (b) = (((rgb) & (0xFF))))

#define DISPLAY_DECOMPOSE_RGB16(rgb, r, g, b) \
    ((r) = (((rgb) >> 11) & 0x1F), (g) = (((rgb) >> 5) & 0x3F), (b) = (((rgb) & (0x1F))))

#define DISPLAY_COMPOSE_RGB(display, r, g, b)                          \
    (((r & display->redMask.mask) << display->redMask.maskShift) |     \
     ((g & display->greenMask.mask) << display->greenMask.maskShift) | \
     ((b & display->blueMask.mask) << display->blueMask.maskShift))

#define DISPLAY_PLOT_8BPP(display, buf, color, x, y)                                           \
    (*((uint8_t*) (buf + ((y) * (display)->bytesPerLine) + ((x) * ((display)->bytesPerPx)))) = \
         color)

#define DISPLAY_PLOT_16BPP(display, buf, color, x, y)                                           \
    (*((uint16_t*) (buf + ((y) * (display)->bytesPerLine) + ((x) * ((display)->bytesPerPx)))) = \
         color)

#define DISPLAY_PLOT_32BPP(display, buf, color, x, y)                                           \
    (*((uint32_t*) (buf + ((y) * (display)->bytesPerLine) + ((x) * ((display)->bytesPerPx)))) = \
         color)

void NkFbConsInit()
{
    NexNixBoot_t* bootArgs = NkGetBootArgs();
    display = &bootArgs->display;
    // Initialze rows / cols
    rows = display->height / 16;
    cols = display->width / 8;
    // Map the backbuffer
    size_t numBufPages =
        ((display->bytesPerLine * display->height) + (NEXKE_CPU_PAGESZ - 1)) / NEXKE_CPU_PAGESZ;
    for (int i = 0; i < numBufPages; ++i)
    {
        MmMulMapEarly (NEXKE_BACKBUF_BASE + (i * NEXKE_CPU_PAGESZ),
                       (paddr_t) display->backBuffer + (i * NEXKE_CPU_PAGESZ),
                       MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_KE);
    }
    display->backBuffer = (void*) NEXKE_BACKBUF_BASE;
    //  Clear the display
    void* backBuf = display->backBuffer;
    for (int i = 0; i < display->height; ++i)
    {
        for (int j = 0; j < display->width; ++j)
        {
            if (display->bpp == 32)
                DISPLAY_PLOT_32BPP (display, backBuf, 0, j, 0);
            else if (display->bpp == 16)
                DISPLAY_PLOT_16BPP (display, backBuf, 0, j, 0);
        }
        backBuf += display->bytesPerLine;
    }
    display->backBufferLoc = display->backBuffer;
    memcpy (display->frameBuffer, display->backBuffer, display->lfbSize);
}

// Invalidate an area
static void fbInvalidate (int x, int y, int width, int height)
{
    // Compute initial location in region
    size_t startLoc = (y * display->bytesPerLine) + (x * display->bytesPerPx);
    int bytesPerPx = display->bpp / 8;
    size_t regionWidth = bytesPerPx * width;
    size_t off = 0;
    // Compute back-buffer specific things
    void* backBufEnd = display->backBuffer + (display->height * display->bytesPerLine);
    void* backBuf = display->backBufferLoc + startLoc;
    if (backBuf >= backBufEnd)
    {
        // Wrap around
        size_t diff = backBuf - backBufEnd;
        backBuf = display->backBuffer + diff;
    }
    // Go through each line in region
    void* front = display->frameBuffer + startLoc;
    for (int i = 0; i < height; ++i)
    {
        if (backBuf >= backBufEnd)
        {
            // Wrap around
            size_t diff = backBuf - backBufEnd;
            backBuf = display->backBuffer + diff;
        }
        // Copy width number of pixels
        memcpy (front, backBuf, regionWidth);
        // Move to next line
        front += display->bytesPerLine;
        backBuf += display->bytesPerLine;
    }
}

static void fbIncRender()
{
    // Update back buffer by a line
    void* end = display->backBuffer + display->lfbSize;
    display->backBufferLoc += display->bytesPerLine;
    if (display->backBufferLoc >= end)
    {
        // Wrap
        size_t diff = display->backBufferLoc - end;
        display->backBufferLoc = display->backBuffer - diff;
    }
}

static void fbConsWriteChar (char c, int col, int row)
{
    // Get color info
    uint32_t pxColor = 0;
    if (display->bpp == 32)
    {
        uint32_t pxColorTmp = COLOR_WHITE32;
        uint8_t r, g, b;
        DISPLAY_DECOMPOSE_RGB (pxColorTmp, r, g, b);
        pxColor = DISPLAY_COMPOSE_RGB (display, r, g, b);
    }
    else if (display->bpp == 16)
    {
        uint16_t pxColorTmp = COLOR_WHITE16;
        uint8_t r, g, b;
        DISPLAY_DECOMPOSE_RGB16 (pxColorTmp, r, g, b);
        pxColor = DISPLAY_COMPOSE_RGB (display, r, g, b);
    }
    uint32_t bgPxColor = 0;
    // Compute glyph
    bool found;
    uint16_t glyphIdx = fb_font_glyph (c, &found);
    // Grab character from font table
    uint8_t* glyph = font + (glyphIdx * 16);
    uint32_t glyphRowSz = (8 + 7) / 8;
    // Compute base offset to character
    uint32_t offset = (row * 16 * display->bytesPerLine) + (col * 8 * display->bytesPerPx);
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
    uint32_t mask = 1 << 8;
    for (int y = 0; y < 16; ++y)
    {
        void* lineBuf = buf;
        uint32_t omask = mask;
        for (int x = 0; x < (8 + 1); ++x)
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
    fbInvalidate (col * 8, row * 16, 8, 16);
}

static bool fbScroll()
{
    // Increment current back buffer base
    for (int i = 0; i < 16; ++i)
        fbIncRender();
    // Invalidate
    fbInvalidate (0, 0, display->width, (rows - 1) * 16);
    // Clear last line
    void* lastLineBuf = display->backBufferLoc + (display->bytesPerLine * ((rows - 1) * 16));
    void* bufEnd = display->backBuffer + display->lfbSize;
    // Wrap if needed
    if (lastLineBuf >= bufEnd)
    {
        uint32_t diff = lastLineBuf - bufEnd;
        lastLineBuf = display->backBuffer + diff;
    }
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < display->width; ++x)
        {
            if (display->bpp == 32)
                DISPLAY_PLOT_32BPP (display, lastLineBuf, 0, x, 0);
            if (display->bpp == 16)
                DISPLAY_PLOT_16BPP (display, lastLineBuf, 0, x, 0);
        }
        lastLineBuf += display->bytesPerLine;
        // Wrap if needed
        if (lastLineBuf >= bufEnd)
        {
            uint32_t diff = lastLineBuf - bufEnd;
            lastLineBuf = display->backBuffer + diff;
        }
    }
    fbInvalidate (0, (rows - 1) * 16, display->width, 16);
    return true;
}

static void fbConsPrintChar (char c)
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
                curCol = cols - 1;
                --curRow;
            }
            else
                curCol = 0;    // Don't go off screen
        }
    }
    else
    {
        fbConsWriteChar (c, curCol, curRow);
        ++curCol;
    }
    // Handle row overflow
    if (curCol >= cols)
    {
        curCol = 0;
        ++curRow;
    }
    // Decide if we need to scroll
    if (curRow >= rows)
    {
        fbScroll();
        curRow = rows - 1;
    }
}

static void fbConsWrite (const char* s)
{
    while (*s)
    {
        fbConsPrintChar (*s);
        ++s;
    }
}

static bool fbConsRead (char* c)
{
    return false;
}

NkConsole_t fbCons = {.read = fbConsRead, .write = fbConsWrite};

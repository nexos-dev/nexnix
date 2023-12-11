/*
    display.h - contains display functions
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

#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <nexboot/driver.h>
#include <nexboot/fw.h>

// Invalidated frame buffer region
typedef struct _frameBufReg
{
    uint16_t startX;    // X corner of region
    uint16_t startY;    // Y corner of region
    uint16_t width;     // Width and height of region
    uint16_t height;
    struct _frameBufReg* next;
} NbInvalidRegion_t;

typedef struct _masksz
{
    uint32_t mask;         // Value to mask component with
    uint32_t maskShift;    // Amount to shift component by
} NbPixelMask_t;

typedef struct _nbDisplay
{
    NbHwDevice_t dev;
    int width;                // Width of seleceted mode
    int height;               // Height of selected mode
    int bytesPerLine;         // Bytes per scanline
    char bpp;                 // Bits per pixel
    NbPixelMask_t redMask;    // Masks
    NbPixelMask_t greenMask;
    NbPixelMask_t blueMask;
    NbPixelMask_t resvdMask;
    void* frontBuffer;                 // Base of front buffer
    void* backBuffer;                  // Base of back buffer
    NbInvalidRegion_t* invalidList;    // Internal. List of regions to copy on buffer
                                       // invalidate
} NbDisplayDev_t;

// EDID timing block
typedef struct _edidBlock
{
    uint16_t timingClock;
    uint8_t xSizeLow;    // Low byte of X size
    uint8_t xBlanking;
    uint8_t xHigh;    // High nibbles of size and blanking
    uint8_t ySizeLow;
    uint8_t yBlanking;
    uint8_t yHigh;
    uint8_t xFrontPorch;
    uint8_t xSyncPulse;
    uint8_t yFrontPorch;
    uint8_t ySyncPulse;
    uint8_t porchSyncHigh;
    uint8_t xSizeMm;
    uint8_t ySizeMm;
    uint8_t mmSizeHigh;
    uint8_t xBorderPx;
    uint8_t yBorderPx;
    uint8_t flags;
} __attribute__ ((packed)) NbEdidTiming_t;

// EDID structure
typedef struct _edid
{
    uint8_t header[8];    // Identifying bytes
    uint16_t manufacturer;
    uint16_t productCode;
    uint8_t serial[4];
    uint8_t weekOrFlag;
    uint8_t year;
    uint8_t version;
    uint8_t revision;
    uint8_t inputDef;
    uint8_t horizSz;
    uint8_t vertSz;
    uint8_t transferFlag;
    uint8_t featSupport;
    uint8_t colorChar[10];
    uint8_t timings[3];    // Established standard timings
    uint8_t stdTimings[16];
    NbEdidTiming_t preferred;
    NbEdidTiming_t optTimings[3];
} __attribute__ ((packed)) NbEdid_t;

// Display mode spec
typedef struct _displayMode
{
    short width;
    short height;
} NbDisplayMode_t;

// Display object functions
#define NB_DISPLAY_INVALIDATE 4
#define NB_DISPLAY_SWAPBUF    5
#define NB_DISPLAY_SETMODE    6

#endif

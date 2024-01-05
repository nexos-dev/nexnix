/*
    efigop.c - contains GOP driver
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

#include <assert.h>
#include <nexboot/driver.h>
#include <nexboot/drivers/display.h>
#include <nexboot/efi/efi.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <string.h>

extern NbObjSvcTab_t gopSvcTab;
extern NbDriver_t gopDrv;

// GOP structure
typedef struct _gopdisplay
{
    NbDisplayDev_t display;
    EFI_HANDLE gopHandle;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* prot;
} NbGopDisplay_t;

// GOP handles list
static EFI_HANDLE* gopHandles = NULL;
static size_t numHandles = 0;
static size_t curHandle = 0;
// GOP GUID
static EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
// EDID GUID
static EFI_GUID edidGuid = EFI_EDID_ACTIVE_PROTOCOL_GUID;

// Gets preferred resoultion on handle
static bool gopGetPreferredRes (EFI_HANDLE handle, int* width, int* height)
{
    EFI_EDID_ACTIVE_PROTOCOL* edidProt = NbEfiOpenProtocol (handle, &edidGuid);
    // Grab EDID
    if (edidProt)
    {
        NbEdid_t* edid = (NbEdid_t*) edidProt->Edid;
        *width = edid->preferred.xSizeLow | ((edid->preferred.xHigh & 0xF0) << 4);
        *height = edid->preferred.ySizeLow | ((edid->preferred.yHigh & 0xF0) << 4);
        NbEfiCloseProtocol (handle, &edidGuid);
    }
    else
    {
        *width = 0, *height = 0;
    }
    return true;
}

// Gets video mode closest to specified width and height
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* gopQueryMode (EFI_GRAPHICS_OUTPUT_PROTOCOL* prot,
                                                           uint32_t* modeNum,
                                                           int width,
                                                           int height,
                                                           bool matchRequired)
{
    uint32_t maxMode = prot->Mode->MaxMode;
    int bestWidth = 0, bestHeight = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* bestMode = NULL;
retry:
    for (int i = 0; i < maxMode; ++i)
    {
        // Get mode information
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* curMode = NULL;
        UINTN sz = 0;
        if (prot->QueryMode (prot, i, &sz, &curMode) != EFI_SUCCESS)
            return NULL;
        // Check if it is supported
        if (curMode->PixelFormat == PixelBltOnly)
            continue;
        if (curMode->HorizontalResolution == width && curMode->VerticalResolution == height)
        {
            // End it
            *modeNum = i;
            return curMode;
        }
        // If mode is greater in size than ideal one, ignore
        if (curMode->HorizontalResolution > width || curMode->VerticalResolution > height)
            continue;
        // Compare difference between this mode and current best mode
        if (((width - curMode->HorizontalResolution) <= (width - bestWidth)) &&
            ((height - curMode->VerticalResolution) <= (height - bestHeight)) && !matchRequired)
        {
            // Set up best mode
            bestHeight = curMode->VerticalResolution;
            bestWidth = curMode->HorizontalResolution;
            *modeNum = i;
            bestMode = curMode;
        }
    }
    return bestMode;
}

// Finds low bit
static int gopGetLowBit (uint32_t val)
{
    int curBit = 0;
    while (!(val & (1 << curBit)))
        ++curBit;
    return curBit;
}

static bool gopSetupDisplay (NbGopDisplay_t* display,
                             EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info,
                             uint32_t modeNum)
{
    // Setup struct
    display->display.width = info->HorizontalResolution;
    display->display.height = info->VerticalResolution;
    display->display.bpp = 32;
    display->display.bytesPerPx = 4;
    display->display.bytesPerLine = info->PixelsPerScanLine * display->display.bytesPerPx;
    display->display.lfbSize = display->display.bytesPerLine * display->display.height;
    // Set masks
    if (info->PixelFormat == PixelBitMask)
    {
        uint32_t redShift = gopGetLowBit (info->PixelInformation.RedMask);
        display->display.redMask.mask = info->PixelInformation.RedMask >> redShift;
        display->display.redMask.maskShift = redShift;
        uint32_t greenShift = gopGetLowBit (info->PixelInformation.GreenMask);
        display->display.greenMask.mask = info->PixelInformation.GreenMask >> greenShift;
        display->display.greenMask.maskShift = greenShift;
        uint32_t blueShift = gopGetLowBit (info->PixelInformation.BlueMask);
        display->display.blueMask.mask = info->PixelInformation.BlueMask >> blueShift;
        display->display.blueMask.maskShift = blueShift;
    }
    else if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
    {
        display->display.redMask.mask = 0xFF;
        display->display.redMask.maskShift = 16;
        display->display.greenMask.mask = 0xFF;
        display->display.greenMask.maskShift = 8;
        display->display.blueMask.mask = 0xFF;
        display->display.blueMask.maskShift = 0;
    }
    else if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
    {
        display->display.redMask.mask = 0xFF;
        display->display.redMask.maskShift = 0;
        display->display.greenMask.mask = 0xFF;
        display->display.greenMask.maskShift = 8;
        display->display.blueMask.mask = 0xFF;
        display->display.blueMask.maskShift = 16;
    }
    else
        assert (0);
    // Set the mode
    display->prot->SetMode (display->prot, modeNum);
    // Set framebuffer
    display->display.frontBuffer = (void*) display->prot->Mode->FrameBufferBase;
    // Allocate backbuffer
    display->display.backBuffer = (void*) NbFwAllocPages (
        (display->display.lfbSize + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE);
    display->display.backBufferLoc = display->display.backBuffer;
    return true;
}

// Driver entry point
static bool EfiGopDrvEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_START:
            // Locate handles
            gopHandles = NbEfiLocateHandle (&gopGuid, &numHandles);
            if (!numHandles)
            {
                NbLogMessage ("nbefigop: GOP unsupported\r\n", NEXBOOT_LOGLEVEL_INFO);
                numHandles = 0;    // Maybe slightly pedantic
            }
            break;
        case NB_DRIVER_ENTRY_DETECTHW: {
        restart:
            if (curHandle == numHandles)
            {
                return false;
            }
            NbGopDisplay_t* display = params;
            memset (display, 0, sizeof (NbGopDisplay_t));
            display->display.dev.devId = curHandle;
            display->display.dev.sz = sizeof (NbGopDisplay_t);
            // Open up protocol
            display->gopHandle = gopHandles[curHandle];
            display->prot = NbEfiOpenProtocol (display->gopHandle, &gopGuid);
            if (!display->prot)
            {
                NbLogMessage ("nbefigop: Unable to open GOP protocol\r\n", NEXBOOT_LOGLEVEL_ERROR);
                ++curHandle;
                goto restart;
            }
            // Get preferred resolution
            int idealWidth = 0, idealHeight = 0;
            uint32_t modeNum = 0;
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mode = NULL;
            gopGetPreferredRes (display->gopHandle, &idealWidth, &idealHeight);
            if (idealWidth && idealHeight)
            {
                // Find closest mode
                mode = gopQueryMode (display->prot, &modeNum, idealWidth, idealHeight, false);
                if (!mode)
                {
                    ++curHandle;
                    goto restart;
                }
            }
            else
            {
                // Go through failsafe modes
#define NUM_FAILSAFES 4
                int failSafes[NUM_FAILSAFES][2] = {{1280, 1024}, {800, 600}};
                for (int i = 0; i < NUM_FAILSAFES; ++i)
                {
                    mode = gopQueryMode (display->prot,
                                         &modeNum,
                                         failSafes[i][0],
                                         failSafes[i][1],
                                         true);
                    if (mode)
                        break;
                }
                if (!mode)
                {
                    // Error out
                    NbLogMessage ("nbefigop: no supported video mode\r\n", NEXBOOT_LOGLEVEL_ERROR);
                    ++curHandle;
                    goto restart;
                }
            }
            // Set it
            gopSetupDisplay (display, mode, modeNum);
            ++curHandle;
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            // Set the interface
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &gopSvcTab);
            NbObjSetManager (obj, &gopDrv);
            break;
        }
    }
    return true;
}

static bool EfiGopDumpData (void* objp, void* data)
{
    void (*write) (const char*, ...) = data;
    NbObject_t* displayObj = objp;
    NbDisplayDev_t* display = NbObjGetData (displayObj);
    write ("Display width: %d\n", display->width);
    write ("Display height: %d\n", display->height);
    write ("Bits per pixel: %d\n", display->bpp);
    return true;
    return true;
}

static bool EfiGopNotify (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbObjNotify_t* notify = params;
    int code = notify->code;
    if (code == NB_DISPLAY_NOTIFY_SETOWNER)
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

static bool EfiGopInvalidate (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbDisplayDev_t* display = NbObjGetData (obj);
    NbInvalidRegion_t* region = params;
    // Validate input
    // Check to ensure startX + width is not greater than line size
    if ((region->startX + region->width) > display->width)
        return false;
    // Ensure region is bounded within display
    if ((region->startY + region->height) > display->height)
        return false;
    // Compute initial location in region
    size_t startLoc =
        (region->startY * display->bytesPerLine) + (region->startX * display->bytesPerPx);
    int bytesPerPx = display->bytesPerPx;
    size_t regionWidth = bytesPerPx * region->width;
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
    void* front = display->frontBuffer + startLoc;
    for (int i = 0; i < region->height; ++i)
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
    return true;
}

static bool EfiGopSetMode (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbGopDisplay_t* display = NbObjGetData (obj);
    NbDisplayMode_t* mode = params;
    // Query mode
    uint32_t modeNum = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* modeInfo =
        gopQueryMode (display->prot, &modeNum, mode->width, mode->height, false);
    if (!modeInfo)
        return false;
    // Unmap current buffers
    size_t lfbPages =
        (display->display.lfbSize + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    // Set mode
    gopSetupDisplay (display, modeInfo, modeNum);
    // Notify owner
    if (obj->owner)
        obj->owner->entry (NB_DISPLAY_CODE_SETMODE, display);
    return true;
}

static bool EfiGopSetRender (void* objp, void* data)
{
    NbObject_t* obj = objp;
    NbDisplayDev_t* display = NbObjGetData (obj);
    // Update back buffer by a line
    void* end = display->backBuffer + display->lfbSize;
    display->backBufferLoc += display->bytesPerLine;
    if (display->backBufferLoc >= end)
    {
        // Wrap
        size_t diff = display->backBufferLoc - end;
        display->backBufferLoc = display->backBuffer - diff;
    }
    return true;
}

// Object interface
static NbObjSvc gopServices[] = {NULL,
                                 NULL,
                                 NULL,
                                 EfiGopDumpData,
                                 EfiGopNotify,
                                 EfiGopInvalidate,
                                 EfiGopSetMode,
                                 EfiGopSetRender};

NbObjSvcTab_t gopSvcTab = {ARRAY_SIZE (gopServices), gopServices};

// Driver structure
NbDriver_t gopDrv = {"EfiGopFb", EfiGopDrvEntry, {0}, 0, false, sizeof (NbGopDisplay_t)};

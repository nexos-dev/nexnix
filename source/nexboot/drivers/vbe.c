/*
    vbe.c - contains VBE modesetting driver
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
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/object.h>
#include <string.h>

extern NbObjSvcTab_t vbeSvcTab;
extern NbDriver_t vbeDrv;

// VBE data structures
typedef struct _vbeBlock
{
    uint8_t sig[4];    // 'VESA'
    uint16_t version;
    uint32_t oemString;
    uint8_t caps[4];        // Flags for info block
    uint16_t vidModeOff;    // seg:offset to video modes
    uint16_t vidModeSeg;
    uint16_t numBlocks;    // Number of 64K memory blocks for video
    uint16_t oemRev;
    uint32_t oemVendor;
    uint32_t oemName;
    uint32_t oemRev2;
    uint8_t resvd[222];
    uint8_t oemData[256];
} __attribute__ ((packed)) VbeInfoBlock_t;

#define VBE3_VERSION  0x0300
#define VBE2_VERSION  0x0200
#define VBE12_VERSION 0x0102
#define VBE1_VERSION  0x0100

#define VBE_DAC_SWITCHABLE (1 << 0)
#define VBE_CTRL_NOT_VGA   (1 << 1)

typedef struct _vbeModeInfo
{
    uint16_t modeAttr;    // Attributes of mode
    uint8_t winAattr;     // Memory window attributes
    uint8_t winBattr;
    uint16_t winGran;    // Granularity of memory windows
    uint16_t winSize;    // Size of memory window
    uint16_t winASeg;
    uint16_t winBSeg;
    uint32_t winFunc;
    uint16_t bytesPerLine;
    uint16_t width;     // X width
    uint16_t height;    // Y height
    uint8_t xCharSz;
    uint8_t yCharSz;
    uint8_t numPlanes;
    uint8_t bitsPerPixel;    // Number of bits per pixel
    uint8_t numBanks;
    uint8_t memModel;    // Memory model
    uint8_t bankSz;
    uint8_t numImages;
    uint8_t resvd;
    uint8_t redMaskSz;    // Now all the masks...
    uint8_t redMaskPos;
    uint8_t greenMaskSz;
    uint8_t greenMaskPos;
    uint8_t blueMaskSz;
    uint8_t blueMaskPos;
    uint8_t resvdMaskSz;
    uint8_t resvdMaskPos;
    uint8_t directColorMode;    // Direct color attributes
    uint32_t frontBuffer;
    uint32_t resvd1;
    uint16_t resvd2;
    uint16_t lfbScanLine;    // LFB bytes per scanline
    uint8_t numBankImages;
    uint8_t numLinImages;
    uint8_t linRedMaskSz;    // Now all the masks...
    uint8_t linRedMaskPos;
    uint8_t linGreenMaskSz;
    uint8_t linGreenMaskPos;
    uint8_t linBlueMaskSz;
    uint8_t linBlueMaskPos;
    uint8_t linResvdMaskSz;
    uint8_t linResvdMaskPos;
    uint32_t maxPixelClock;    // Max pixel clock
    uint8_t resvd3[189];
} __attribute__ ((packed)) VbeModeInfo_t;

#define VBE_MODE_SUPPORTED (1 << 0)
#define VBE_MODE_COLOR     (1 << 3)
#define VBE_MODE_GRAPHICS  (1 << 4)
#define VBE_MODE_VGA       (1 << 5)
#define VBE_MODE_WINDOWED  (1 << 6)
#define VBE_MODE_LFB       (1 << 7)

#define VBE_MODEL_TEXT        0
#define VBE_MODEL_CGA         1
#define VBE_MODEL_HERCULES    2
#define VBE_MODEL_PLANAR      3
#define VBE_MODEL_PACKED      4
#define VBE_MODEL_DIRECTCOLOR 6

#define VBE_MODENUM_LFB (1 << 14)

#define VBE_SUPPORTED 0x4F
#define VBE_SUCCESS   0

#define VBE_GET_CTRL 0
#define VBE_GET_MODE 1
#define VBE_SET_MODE 2
#define VBE_DDC_FUNC 0x15
#define VBE_DDC_EDID 1

// Is VBE even enabled?
static bool vbeEnabled = true;

// VBE version we are working with
static uint8_t vbeVer = 0;

// VBE modes pointer
static uint16_t* modes = NULL;

// backbuffer size
static uint32_t backSize = 0;

// VBE BIOS helpers

// Gets VBE controller information
static bool vbeGetCtrlBlock (VbeInfoBlock_t* outSt)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x4F;
    in.al = VBE_GET_CTRL;
    in.es = 0;
    in.di = NEXBOOT_BIOSBUF_BASE;
    NbBiosCall (0x10, &in, &out);
    // Check status
    if (out.al != VBE_SUPPORTED)
        return false;
    if (out.ah != VBE_SUCCESS)
        return false;
    // Copy it
    memcpy (outSt, (void*) NEXBOOT_BIOSBUF_BASE, sizeof (VbeInfoBlock_t));
    return true;
}

// Gets VBE mode info
static bool vbeGetModeInfo (uint16_t mode, VbeModeInfo_t* outSt)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x4F;
    in.al = VBE_GET_MODE;
    in.es = 0;
    in.di = NEXBOOT_BIOSBUF_BASE;
    in.cx = mode;
    NbBiosCall (0x10, &in, &out);
    // Check result
    if (out.al != VBE_SUPPORTED)
        return false;
    if (out.ah != VBE_SUCCESS)
        return false;
    // Copy result
    memcpy (outSt, (void*) NEXBOOT_BIOSBUF_BASE, sizeof (VbeModeInfo_t));
    return true;
}

// Sets VBE mode
static bool vbeSetMode (uint16_t mode)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x4F;
    in.al = VBE_SET_MODE;
    in.bx = mode | VBE_MODENUM_LFB;
    NbBiosCall (0x10, &in, &out);
    if (out.al != VBE_SUPPORTED)
        return false;
    if (out.ah != VBE_SUCCESS)
        return false;
    return true;
}

// Gets EDID info
static bool vbeGetEdid (NbEdid_t* edid)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x4F;
    in.al = VBE_DDC_FUNC;
    in.bl = VBE_DDC_EDID;
    in.di = NEXBOOT_BIOSBUF_BASE;
    NbBiosCall (0x10, &in, &out);
    if (out.al != VBE_SUPPORTED)
        return false;
    if (out.ah != VBE_SUCCESS)
        return false;
    // Copy it out
    memcpy (edid, (void*) NEXBOOT_BIOSBUF_BASE, sizeof (NbEdid_t));
    return true;
}

// Gets ideal resolution
static void vbeGetPreferredRes (int* width, int* height)
{
    // Try to read EDID
    NbEdid_t edid = {0};
    if (vbeGetEdid (&edid))
    {
        // Read in resolution
        *width = edid.preferred.xSizeLow | ((edid.preferred.xHigh & 0xF0) << 4);
        *height = edid.preferred.ySizeLow | ((edid.preferred.yHigh & 0xF0) << 4);
        if (!*width || !*height)
            *width = 640, *height = 480;    // Fix invalid EDID results
    }
    else
    {
        // Default to 640x480
        *width = 640;
        *height = 480;
    }
}

// Maps frame buffer
static void vbeMapBuffer (NbDisplayDev_t* display, void* buf)
{
    size_t lfbSize = display->bytesPerLine * display->height;
    size_t lfbPages = (lfbSize + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    for (int i = 0; i < lfbPages; ++i)
    {
        NbCpuAsMap ((uintptr_t) buf + (i * NEXBOOT_CPU_PAGE_SIZE),
                    (paddr_t) (buf + (i * NEXBOOT_CPU_PAGE_SIZE)),
                    NB_CPU_AS_RW | NB_CPU_AS_WT);
    }
}

// Sets up display
static void vbeSetupDisplay (NbDisplayDev_t* display, VbeModeInfo_t* modeInfo, uint16_t modeNum)
{
    // Setup display struct
    display->invalidList = NULL;
    display->width = modeInfo->width;
    display->height = modeInfo->height;
    display->bpp = modeInfo->bitsPerPixel;
    display->bytesPerPx = display->bpp / 8;
    if (vbeVer == 3)
        display->bytesPerLine = modeInfo->lfbScanLine;
    else
        display->bytesPerLine = modeInfo->bytesPerLine;
    display->frontBuffer = (void*) modeInfo->frontBuffer;
    // Set masks
    if (modeInfo->memModel == VBE_MODEL_DIRECTCOLOR)
    {
        display->redMask.mask =
            (vbeVer == 3) ? ((1 << modeInfo->linRedMaskSz) - 1) : ((1 << modeInfo->redMaskSz) - 1);
        display->greenMask.mask = (vbeVer == 3) ? ((1 << modeInfo->linGreenMaskSz) - 1)
                                                : ((1 << modeInfo->greenMaskSz) - 1);
        display->blueMask.mask = (vbeVer == 3) ? ((1 << modeInfo->linBlueMaskSz) - 1)
                                               : ((1 << modeInfo->blueMaskSz) - 1);
        display->redMask.maskShift = (vbeVer == 3) ? modeInfo->linRedMaskPos : modeInfo->redMaskPos;
        display->greenMask.maskShift =
            (vbeVer == 3) ? modeInfo->linGreenMaskPos : modeInfo->greenMaskPos;
        display->blueMask.maskShift =
            (vbeVer == 3) ? modeInfo->linBlueMaskPos : modeInfo->blueMaskPos;
    }
    else
    {
        // Base it on BPP
        if (display->bpp == 32)
        {
            display->redMask.mask = 0xFF;
            display->greenMask.mask = 0xFF;
            display->blueMask.mask = 0xFF;
            display->redMask.maskShift = 16;
            display->greenMask.maskShift = 8;
            display->blueMask.maskShift = 0;
        }
        else if (display->bpp == 16)
        {
            display->redMask.mask = 0x1F;
            display->greenMask.mask = 0x1F;
            display->blueMask.mask = 0x1F;
            display->redMask.maskShift = 10;
            display->greenMask.maskShift = 5;
            display->blueMask.maskShift = 0;
        }
        else if (display->bpp == 8)
        {
            display->redMask.mask = 0x7;
            display->greenMask.mask = 0x7;
            display->blueMask.mask = 0x3;
            display->redMask.maskShift = 5;
            display->greenMask.maskShift = 2;
            display->blueMask.maskShift = 0;
        }
    }
    // Map the framebuffer
    vbeMapBuffer (display, display->frontBuffer);
    // Map back buffer. We put it at the end of nexboot
    display->backBuffer = (void*) NEXBOOT_BIOS_END;
    display->backBufferLoc = display->backBuffer;
    vbeMapBuffer (display, display->backBuffer);
    // Set size
    size_t lfbSize = display->bytesPerLine * display->height;
    display->lfbSize = lfbSize;
    backSize = lfbSize;
    // Set VBE mode
    vbeSetMode (modeNum);
    // Clear buffers
    memset (display->backBuffer, 0, lfbSize);
    memset (display->frontBuffer, 0, lfbSize);
}

// Querys availibilty of specified mode
static bool vbeQueryMode (uint16_t width,
                          uint16_t height,
                          uint16_t* modeNum,
                          VbeModeInfo_t* modeOut)
{
    VbeModeInfo_t modeInfo = {0};
    uint16_t bestModeNum = 0;
    int bestWidth = 0, bestHeight = 0;
    uint16_t* modesIter = modes;
    bool modeFound = false;
    while (*modesIter != 0xFFFF)
    {
        uint16_t mode = *modesIter;
        // Get mode info
        if (!vbeGetModeInfo (mode, &modeInfo))
            continue;    // Move to next mode
        // Check if this mode is supported
        if (!(modeInfo.modeAttr & VBE_MODE_SUPPORTED))
            goto next;
        if (!(modeInfo.modeAttr & VBE_MODE_COLOR))
            goto next;
        if (!(modeInfo.modeAttr & VBE_MODE_GRAPHICS))
            goto next;
        if (!(modeInfo.modeAttr & VBE_MODE_LFB))
            goto next;
        if (modeInfo.memModel != VBE_MODEL_PACKED && modeInfo.memModel != VBE_MODEL_DIRECTCOLOR)
        {
            goto next;
        }
        if (modeInfo.bitsPerPixel != 16 && modeInfo.bitsPerPixel != 32)
            goto next;    // 24 and 8 bpp not supported
        // If mode is identical, use it
        if (modeInfo.width == width && modeInfo.height == height && modeInfo.bitsPerPixel == 32)
        {
            // End it
            modeFound = true;
            bestHeight = modeInfo.height;
            bestWidth = modeInfo.width;
            *modeNum = *modesIter;
            memcpy (modeOut, &modeInfo, sizeof (VbeModeInfo_t));
            break;
        }
        // If mode is greater in size than ideal one, ignore
        if (modeInfo.width > width || modeInfo.height > height)
            goto next;
        // Compare difference between this mode and current best mode
        if (((width - modeInfo.width) <= (width - bestWidth)) &&
            ((height - modeInfo.height) <= (height - bestHeight)))
        {
            // Set up best mode
            modeFound = true;
            bestHeight = modeInfo.height;
            bestWidth = modeInfo.width;
            *modeNum = *modesIter;
            memcpy (modeOut, &modeInfo, sizeof (VbeModeInfo_t));
        }
    next:
        ++modesIter;
    }
    return modeFound;
}

// Driver entry point
static bool VbeDrvEntry (int code, void* params)
{
    switch (code)
    {
        case NB_DRIVER_ENTRY_START:
// Set VBE enable state
#ifndef NEXNIX_GRAPHICS_GRAPHICAL
            vbeEnabled = false;
#endif
            break;
        case NB_DRIVER_ENTRY_DETECTHW: {
            if (!vbeEnabled)
                return false;    // Don't worry about it
            // Attempt to find VBE controller block
            VbeInfoBlock_t block = {0};
            memcpy (block.sig, "VBE2", 4);
            if (!vbeGetCtrlBlock (&block))
            {
                NbLogMessageEarly ("vbe: no controller block found\r\n", NEXBOOT_LOGLEVEL_ERROR);
                return false;
            }
            // Check signature
            if (memcmp (block.sig, "VESA", 4))
            {
                NbLogMessageEarly ("vbe: controller block corrupted\r\n", NEXBOOT_LOGLEVEL_ERROR);
                return false;
            }
            // Ensure version is compatible with us
            if (block.version < VBE2_VERSION)
            {
                NbLogMessage ("vbe: VBE 2.0+ required\r\n", NEXBOOT_LOGLEVEL_ERROR);
                return false;
            }
            if (block.version == VBE2_VERSION)
                vbeVer = 2;
            else if (block.version == VBE3_VERSION)
                vbeVer = 3;
            // Copy modes array
            uint16_t* rmModes = (uint16_t*) ((block.vidModeSeg * 0x10) + block.vidModeOff);
            // Get size
            uint16_t* modesEnd = rmModes;
            while (*modesEnd != 0xFFFF)
                ++modesEnd;
            // Copy it
            size_t modesSz = ((void*) modesEnd - (void*) rmModes) + 2;
            modes = malloc (modesSz);
            memcpy (modes, rmModes, modesSz);
            // Get preferred mode;
            int idealWidth = 0, idealHeight = 0;
            vbeGetPreferredRes (&idealWidth, &idealHeight);
            // Iterate on modes, trying to find best one
            VbeModeInfo_t modeInfo = {0};
            uint16_t bestModeNum = 0;
            if (!vbeQueryMode (idealWidth, idealHeight, &bestModeNum, &modeInfo))
                return false;
            // Set it
            vbeSetupDisplay (params, &modeInfo, bestModeNum);
            break;
        }
        case NB_DRIVER_ENTRY_ATTACHOBJ: {
            // Set the interface
            NbObject_t* obj = params;
            NbObjInstallSvcs (obj, &vbeSvcTab);
            NbObjSetManager (obj, &vbeDrv);
            break;
        }
    }
    return true;
}

static bool VbeObjDumpData (void* objp, void* params)
{
    void (*write) (const char*, ...) = params;
    NbObject_t* displayObj = objp;
    NbDisplayDev_t* display = NbObjGetData (displayObj);
    write ("Display width: %d\n", display->width);
    write ("Display height: %d\n", display->height);
    write ("Bits per pixel: %d\n", display->bpp);
    return true;
}

static bool VbeObjNotify (void* objp, void* params)
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

static bool VbeObjInvalidate (void* objp, void* params)
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
    int bytesPerPx = display->bpp / 8;
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

static bool VbeObjSetMode (void* objp, void* params)
{
    NbObject_t* obj = objp;
    NbDisplayDev_t* display = NbObjGetData (obj);
    NbDisplayMode_t* mode = params;
    // Query mode
    uint16_t modeNum = 0;
    VbeModeInfo_t modeInfo = {0};
    if (!vbeQueryMode (mode->width, mode->height, &modeNum, &modeInfo))
        return false;
    // Unmap current buffers
    size_t lfbPages = (display->lfbSize + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    for (int i = 0; i < lfbPages; ++i)
    {
        NbCpuAsUnmap ((uintptr_t) display->frontBuffer + (i * NEXBOOT_CPU_PAGE_SIZE));
        NbCpuAsUnmap ((uintptr_t) display->backBuffer + (i * NEXBOOT_CPU_PAGE_SIZE));
    }
    // Set mode
    vbeSetupDisplay (display, &modeInfo, modeNum);
    // Notify owner
    if (obj->owner)
        obj->owner->entry (NB_DISPLAY_CODE_SETMODE, display);
    return true;
}

static bool VbeObjSetRender (void* objp, void* params)
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

static bool VbeObjUnmapFb (void* objp, void* params)
{
    NbLogMessage ("nexboot: mapping framebuffer to %#lX\n",
                  NEXBOOT_LOGLEVEL_DEBUG,
                  NEXBOOT_FB_BASE);
    NbObject_t* obj = objp;
    NbDisplayDev_t* display = NbObjGetData (obj);
    size_t lfbPages = (display->lfbSize + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
    for (int i = 0; i < lfbPages; ++i)
    {
        NbCpuAsUnmap ((uintptr_t) display->frontBuffer + (i * NEXBOOT_CPU_PAGE_SIZE));
        // Map framebuffer to new address
        NbCpuAsMap ((uintptr_t) NEXBOOT_FB_BASE + (i * NEXBOOT_CPU_PAGE_SIZE),
                    (uintptr_t) display->frontBuffer + (i * NEXBOOT_CPU_PAGE_SIZE),
                    NB_CPU_AS_RW | NB_CPU_AS_WT);
        // NbCpuAsUnmap ((uintptr_t) display->backBuffer + (i * NEXBOOT_CPU_PAGE_SIZE));
    }
    display->frontBuffer = (void*) NEXBOOT_FB_BASE;
    return true;
}

// Helper to get end of backbuffer area
uintptr_t NbBiosGetBootEnd()
{
    if (!vbeEnabled)
        return NEXBOOT_BIOS_END;
    else
        return NEXBOOT_BIOS_END + NbPageAlignUp (backSize);
}

// Object interface
static NbObjSvc vbeServices[] = {NULL,
                                 NULL,
                                 NULL,
                                 VbeObjDumpData,
                                 VbeObjNotify,
                                 VbeObjInvalidate,
                                 VbeObjSetMode,
                                 VbeObjSetRender,
                                 VbeObjUnmapFb};

NbObjSvcTab_t vbeSvcTab = {ARRAY_SIZE (vbeServices), vbeServices};

// Driver structure
NbDriver_t vbeDrv = {"VbeFb", VbeDrvEntry, {0}, 0, false, sizeof (NbDisplayDev_t)};

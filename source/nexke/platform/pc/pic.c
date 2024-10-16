/*
    pic.c - contains 8259A PIC driver
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

#include <assert.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/pc.h>
#include <stdlib.h>
#include <string.h>

// PIC registers
#define PLT_PIC_MASTER_CMD    0x20
#define PLT_PIC_MASTER_STATUS 0x20
#define PLT_PIC_MASTER_DATA   0x21
#define PLT_PIC_SLAVE_CMD     0xA0
#define PLT_PIC_SLAVE_STATUS  0xA0
#define PLT_PIC_SLAVE_DATA    0xA1

// ELCR
#define PLT_PIC_ELCR 0x4D0
static bool isElcr = false;

// ICW1 bits
#define PLT_PIC_ICW4   (1 << 0)    // Should it expect ICW4
#define PLT_PIC_SINGLE (1 << 1)    // Should it run in single PIC mode
#define PLT_PIC_LTIM   (1 << 3)    // Should it be level-triggered
#define PLT_PIC_INIT   (1 << 4)    // Initializes the PIC

// ICW4 bits
#define PLT_PIC_X86  (1 << 0)    // PIC should be in x86 mode
#define PLT_PIC_AEOI (1 << 1)    // Automatically send EOI

// OCW2 bits
// The only OCW2 thing we care about is EOI
#define PLT_PIC_EOI (1 << 5)
// OCW3
#define PLT_PIC_READISR 0x0B

// Gets a mask from an IPL
static uint16_t pltPicIplMap[] = {0,
                                  0x8000,
                                  0xC000,
                                  0xE000,
                                  0xF000,
                                  0xF800,
                                  0xFC00,
                                  0xFE00,
                                  0xFF00,
                                  0xFF80,
                                  0xFFC0,
                                  0xFFE0,
                                  0xFFF0,
                                  0xFFF8,
                                  0xFFFC,
                                  0xFFFC,
                                  0xFFFE};
#define PLT_PIC_IPL_RANGE 16

// Maps priorities to IPLs
static uint8_t pltPicPrioMap[] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

extern PltHwIntCtrl_t plt8259A;

// Reads ISR of both PICs
static inline uint16_t PltPicGetIsr()
{
    CpuOutb (PLT_PIC_MASTER_CMD, PLT_PIC_READISR);
    CpuOutb (PLT_PIC_SLAVE_CMD, PLT_PIC_READISR);
    return (CpuInb (PLT_PIC_SLAVE_CMD) << 8) | CpuInb (PLT_PIC_MASTER_CMD);
}

// Begins processing an interrupt
static bool PltPicBeginInterrupt (NkCcb_t* ccb, int vector)
{
    if (vector == CPU_BASE_HWINT + 7)
    {
        if (!PltPicGetIsr() & (1 << 7))
        {
            CpuOutb (PLT_PIC_MASTER_CMD, PLT_PIC_EOI);    // Send EOI
            return false;
        }
    }
    else if (vector == CPU_BASE_HWINT + 15)
    {

        if (!PltPicGetIsr() & (1 << 15))
        {
            CpuOutb (PLT_PIC_MASTER_CMD, PLT_PIC_EOI);    // Send EOI
            CpuOutb (PLT_PIC_SLAVE_CMD, PLT_PIC_EOI);
            return false;
        }
    }
    return true;
}

// Ends processing of an interrupt
static void PltPicEndInterrupt (NkCcb_t* ccb, int vector)
{
    // Send EOI to PIC
    if ((vector - CPU_BASE_HWINT) >= 8)
        CpuOutb (PLT_PIC_SLAVE_CMD, PLT_PIC_EOI);
    // Master gets EOI either way
    CpuOutb (PLT_PIC_MASTER_CMD, PLT_PIC_EOI);
}

// Masks specified interrupt
static void PltPicDisableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    uint32_t gsi = intObj->gsi;
    uint8_t picPort = PLT_PIC_MASTER_DATA;
    if (gsi >= 8)
    {
        // Set things for slave PIC
        gsi -= 8;
        picPort = PLT_PIC_SLAVE_DATA;
    }
    uint8_t mask = CpuInb (picPort) | (1 << gsi);
    CpuOutb (picPort, mask);
}

// Unmasks specified interrupt
static void PltPicEnableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    uint32_t gsi = intObj->gsi;
    uint8_t picPort = PLT_PIC_MASTER_DATA;
    if (gsi >= 8)
    {
        // Set things for slave PIC
        gsi -= 8;
        picPort = PLT_PIC_SLAVE_DATA;
    }
    uint8_t mask = CpuInb (picPort) & ~(1 << gsi);
    CpuOutb (picPort, mask);
}

// Sets IPL to specified level
static void PltPicSetIpl (NkCcb_t* ccb, ipl_t ipl)
{
    // Get IPL in range
    if (ipl > PLT_PIC_IPL_RANGE)
        ipl = PLT_PIC_IPL_RANGE;
    // Get PIC mask
    uint16_t mask = pltPicIplMap[ipl];
    // Throw in the saved mask
    mask |= (CpuInb (PLT_PIC_MASTER_DATA) | (CpuInb (PLT_PIC_SLAVE_DATA) << 8));
    // Set the mask
    CpuOutb (PLT_PIC_MASTER_DATA, (uint8_t) mask);
    CpuOutb (PLT_PIC_SLAVE_DATA, mask >> 8);
}

// Connects interrupt to specified vector
static int PltPicConnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* hwInt)
{
    if (!isElcr && hwInt->mode == PLT_MODE_LEVEL)
    {
        NkLogDebug ("nexke: attempt to install level-trigerred interrupt, ignoring\n");
        return -1;    // Error
    }
    // Get existing chain
    assert (hwInt->gsi < 16);
    PltHwIntChain_t* chain = &plt8259A.lineMap[hwInt->gsi];
    NkSpinLock (&chain->lock);
    if (NkListFront (&chain->list))
    {
        NkHwInterrupt_t* int2 = LINK_CONTAINER (NkListFront (&chain->list), NkHwInterrupt_t, link);
        if (int2->mode != hwInt->mode)
        {
            NkSpinUnlock (&chain->lock);
            return -1;    // Can't mix modes
        }
    }
    // Set as edge or level if needed
    if (!NkListFront (&chain->list))
    {
        uint16_t elcr = CpuInb (PLT_PIC_ELCR) | (CpuInb (PLT_PIC_ELCR + 1) << 8);
        if (hwInt->mode == PLT_MODE_LEVEL)
            elcr |= (1 << hwInt->gsi);
        else
            elcr &= ~(1 << hwInt->gsi);
        CpuOutb (PLT_PIC_ELCR, elcr & 0xFF);
        CpuOutb (PLT_PIC_ELCR + 1, elcr >> 8);
    }
    NkSpinUnlock (&chain->lock);
    // Set the IPL. FORCE_IPL is ignored beacuse we have no control over IPL with 8259A
    hwInt->ipl = pltPicPrioMap[hwInt->gsi];
    return hwInt->gsi + CPU_BASE_HWINT;
}

// Disconnects interrupt from specified vector
static void PltPicDisconnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* hwInt)
{
    assert (hwInt->gsi < 16);
    PltHwIntChain_t* chain = &plt8259A.lineMap[hwInt->gsi];
    if (chain->chainLen == 0)
        PltPicDisableInterrupt (ccb, hwInt);
}

// Basic structure
PltHwIntCtrl_t plt8259A = {.type = PLT_HWINT_8259A,
                           .beginInterrupt = PltPicBeginInterrupt,
                           .endInterrupt = PltPicEndInterrupt,
                           .disableInterrupt = PltPicDisableInterrupt,
                           .enableInterrupt = PltPicEnableInterrupt,
                           .setIpl = PltPicSetIpl,
                           .connectInterrupt = PltPicConnectInterrupt,
                           .disconnectInterrupt = PltPicDisconnectInterrupt};

// Intialization
PltHwIntCtrl_t* PltPicInit()
{
    NkLogDebug ("nexke: Using 8259A as interrupt controller\n");
    // Send ICW1
    CpuOutb (PLT_PIC_MASTER_CMD, PLT_PIC_ICW4 | PLT_PIC_INIT);
    CpuOutb (PLT_PIC_SLAVE_CMD, PLT_PIC_ICW4 | PLT_PIC_INIT);
    // Map interrupts to CPU_BASE_HWINT
    CpuOutb (PLT_PIC_MASTER_DATA, CPU_BASE_HWINT);
    CpuOutb (PLT_PIC_SLAVE_DATA, CPU_BASE_HWINT + 8);
    // Inform master and slave of their connection via IR line 2
    CpuOutb (PLT_PIC_MASTER_DATA, (1 << 2));
    CpuOutb (PLT_PIC_SLAVE_DATA, 2);
    // Send ICW4
    CpuOutb (PLT_PIC_MASTER_DATA, PLT_PIC_X86);
    CpuOutb (PLT_PIC_SLAVE_DATA, PLT_PIC_X86);
    // Mask all interrupts by default
    // Except for the cascading ones
    CpuOutb (PLT_PIC_MASTER_DATA, 0xFB);
    CpuOutb (PLT_PIC_SLAVE_DATA, 0xFF);
    // Cofigure ELCR
    uint16_t elcr = CpuInb (PLT_PIC_ELCR) | (CpuInb (PLT_PIC_ELCR + 1) << 8);
    // If bits 0, 1, 2, and 13 are clear ELCR is supported
    if (!(elcr & ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 13))))
        isElcr = true;
    else
        NkLogDebug ("nexke: no ELCR found, only edge-triggered interrupts are supported\n");
    // Set up line map
    size_t mapSz = sizeof (PltHwIntChain_t) * 16;
    plt8259A.lineMap = (PltHwIntChain_t*) kmalloc (mapSz);
    plt8259A.mapEntries = 16;
    assert (plt8259A.lineMap);
    memset (plt8259A.lineMap, 0, mapSz);
    return &plt8259A;
}

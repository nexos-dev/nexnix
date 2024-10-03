/*
    interrupt.c - contains interrupt dispatcher
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
#include <nexke/cpu.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Interrupt table
static NkInterrupt_t* nkIntTable[NK_MAX_INTS] = {NULL};

// Slab cache
static SlabCache_t* nkIntCache = NULL;

// Platform static pointer
static NkPlatform_t* platform = NULL;

// Installs an interrupt handler
NkInterrupt_t* PltInstallInterrupt (int vector,
                                    int type,
                                    PltIntHandler hndlr,
                                    NkHwInterrupt_t* hwInt)
{
    // Ensure vector is free
    if (nkIntTable[vector])
        return NULL;    // Interrupt is in use
    NkInterrupt_t* obj = (NkInterrupt_t*) MmCacheAlloc (nkIntCache);
    if (!obj)
        NkPanic ("nexke: out of memory");
    // Disable interrupts, we are in critical code
    CpuDisable();
    // Initialize object
    obj->callCount = 0;
    assert (hndlr);
    obj->handler = hndlr;
    obj->type = type;
    obj->vector = vector;
    if (hwInt)
    {
        assert (obj->type == PLT_INT_HWINT);
        memcpy (&obj->hwInt, hwInt, sizeof (NkHwInterrupt_t));
        // Enable it
        platform->intCtrl->enableInterrupt (CpuGetCcb(), &obj->hwInt);
    }
    // Insert in table
    nkIntTable[vector] = obj;
    CpuEnable();
    return obj;
}

// Connects an interrupt to hardware controller
int PltConnectInterrupt (NkHwInterrupt_t* hwInt)
{
    CpuDisable();
    int vector = platform->intCtrl->connectInterrupt (CpuGetCcb(), hwInt);
    CpuEnable();
    return vector;
}

// Uninstalls an interrupt handler
void PltUninstallInterrupt (NkInterrupt_t* intObj)
{
    assert (intObj);
    if (!nkIntTable[intObj->vector])
        NkPanic ("nexke: can't uninstall non-existant interrupt");
    CpuDisable();
    if (intObj->type == PLT_INT_HWINT)
    {
        // Disconnect and disable
        platform->intCtrl->disconnectInterrupt (CpuGetCcb(), &intObj->hwInt);
    }
    nkIntTable[intObj->vector] = NULL;
    CpuEnable();
    MmCacheFree (nkIntCache, intObj);
}

// Initializes interrupt system
void PltInitInterrupts()
{
    // Store platform pointer
    platform = PltGetPlatform();
    // Create cache
    nkIntCache = MmCacheCreate (sizeof (NkInterrupt_t), NULL, NULL);
    assert (nkIntCache);
    // Register CPU exception handlers
    CpuRegisterExecs();
}

// Raises IPL to specified level
ipl_t PltRaiseIpl (ipl_t newIpl)
{
    CpuDisable();    // For safety
    NkCcb_t* ccb = CpuGetCcb();
    if (ccb->curIpl >= newIpl)
        NkPanic ("nexke: invalid IPL to raise to");
    ipl_t oldIpl = ccb->curIpl;
    ccb->curIpl = newIpl;    // Set IPL
    // Re-enable if needed
    if (newIpl != PLT_IPL_HIGH)
    {
        platform->intCtrl->setIpl (ccb, newIpl);    // Do it on the hardware side
        CpuEnable();
    }
    return oldIpl;
}

// Lowers IPL back to level
void PltLowerIpl (ipl_t oldIpl)
{
    CpuDisable();    // For safety
    NkCcb_t* ccb = CpuGetCcb();
    if (ccb->curIpl <= oldIpl)
        NkPanic ("nexke: Invalid IPL to lower to");
    if (ccb->curIpl == PLT_IPL_HIGH)
        CpuEnable();         // Make sure the int disable counter is correct
    ccb->curIpl = oldIpl;    // Restore it
    // Re-enable if needed
    if (oldIpl != PLT_IPL_HIGH)
    {
        platform->intCtrl->setIpl (ccb, oldIpl);    // Do it on the hardware side
        CpuEnable();
    }
}

// Called when a trap goes bad and the system needs to crash
void PltBadTrap (CpuIntContext_t* context, const char* msg, ...)
{
    va_list ap;
    va_start (ap, msg);
    // Print the info
    NkLogMessage ("nexke: bad trap: ", NK_LOGLEVEL_EMERGENCY, ap);
    NkLogMessage (msg, NK_LOGLEVEL_EMERGENCY, ap);
    NkLogMessage ("\n", NK_LOGLEVEL_EMERGENCY, ap);
// If we are in a debug build, print debugging info
#ifndef NDEBUG
    CpuPrintDebug (context);
#endif
    va_end (ap);
    // Crash the system
    CpuCrash();
}

// Exception dispatcher. Called when first level handling fails
void PltExecDispatch (NkInterrupt_t* intObj, CpuIntContext_t* context)
{
    // For now this is simple. We just always crash
    CpuExecInf_t execInf = {0};
    CpuGetExecInf (&execInf, intObj, context);
    PltBadTrap (context, "%s", execInf.name);
}

// Trap dispatcher
void PltTrapDispatch (CpuIntContext_t* context)
{
    NkCcb_t* ccb = CpuGetCcb();
    // Grab the interrupt object
    NkInterrupt_t* intObj = nkIntTable[CPU_CTX_INTNUM (context)];
    if (!intObj)
    {
        // First see if hardware interrupt controller knows how to handle it
        NkHwInterrupt_t hwInt = {0};
        hwInt.line = CPU_CTX_INTNUM (context);
        hwInt.flags = PLT_HWINT_FAKE;
        if (!platform->intCtrl->beginInterrupt (ccb, &hwInt))
        {
            // It handled it, see if we need to increate spurious counter
            if (hwInt.flags & PLT_HWINT_SPURIOUS)
                ++ccb->spuriousInts;
            return;    // We are done
        }
        else
        {
            // Unhandled interrupt, that's a bad trap
            PltBadTrap (context, "unhandled interrupt %#X", CPU_CTX_INTNUM (context));
        }
    }
    // Now we need to determine what kind of trap this is. There are 3 possibilities
    // If this is an exception, first we call the registered handler. If the handler fails to handle
    // the int, then we call PltExecDispatch to perform default processing
    // If this is a service, we call the handler and then we are finished
    // If this is a hardware interrupt, our job is more complicated.
    // First we must set the IPL to the level of the interrupt, and then we enable interrupts
    // from the hardware. Then we check we must check if it's spurious. If not, then we call the
    // handler function before finally telling the interrupt hardware to end the interrupt
    if (intObj->type == PLT_INT_EXEC)
    {
        ++intObj->callCount;
        // First call the handler (if one exists) and see if it resolves it
        bool resolved = false;
        if (intObj->handler)
            resolved = intObj->handler (intObj, context);
        if (!resolved)
        {
            // This means this exception is in error. We must call the exception dispatcher
            PltExecDispatch (intObj, context);
        }
    }
    else if (intObj->type == PLT_INT_SVC)
    {
        ++intObj->callCount;
        intObj->handler (intObj, context);    // This will never fail
    }
    else if (intObj->type == PLT_INT_HWINT)
    {
        // Set the IPL and enable interrupts
        ipl_t oldIpl = ccb->curIpl;
        ccb->curIpl = intObj->hwInt.ipl;
        CpuEnable();
        // Check if this interrupt is spurious
        if (!platform->intCtrl->beginInterrupt (ccb, &intObj->hwInt))
        {
            // This interrupt is spurious. Increase counter and return
            ++ccb->spuriousInts;
        }
        else
        {
            // Handle
            ++intObj->callCount;
            intObj->handler (intObj, context);
            CpuDisable();
            // End the interrupt
            platform->intCtrl->endInterrupt (ccb, &intObj->hwInt);
        }
        // Restore IPL
        ccb->curIpl = oldIpl;
    }
    else
        assert (!"Invalid interrupt type");
}

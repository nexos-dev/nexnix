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
static SlabCache_t* nkHwIntCache = NULL;

// Platform static pointer
static NkPlatform_t* platform = NULL;

// Interrupt allocation
static inline NkInterrupt_t* pltAllocInterrupt (int vector, int type)
{
    // Ensure vector is free
    if (nkIntTable[vector])
        return NULL;    // Interrupt is in use
    NkInterrupt_t* obj = (NkInterrupt_t*) MmCacheAlloc (nkIntCache);
    if (!obj)
        NkPanic ("nexke: out of memory");
    // Initialize object
    obj->callCount = 0;
    obj->type = type;
    obj->vector = vector;
    // Insert in table
    nkIntTable[vector] = obj;
    return obj;
}

// Installs an exception handler
NkInterrupt_t* PltInstallExec (int vector, PltIntHandler hndlr)
{
    CpuDisable();
    NkInterrupt_t* obj = pltAllocInterrupt (vector, PLT_INT_EXEC);
    obj->handler = hndlr;
    CpuEnable();
    return obj;
}

// Installs a service handler
NkInterrupt_t* PltInstallSvc (int vector, PltIntHandler hndlr)
{
    CpuDisable();
    NkInterrupt_t* obj = pltAllocInterrupt (vector, PLT_INT_SVC);
    obj->handler = hndlr;
    CpuEnable();
    return obj;
}

// Installs a hardware interrupt
NkInterrupt_t* PltInstallInterrupt (int vector, NkHwInterrupt_t* hwInt)
{
    CpuDisable();
    // Check if interrupt is installed
    NkInterrupt_t* obj = nkIntTable[vector];
    if (obj)
    {
        // Chain it
        // TODO
    }
    else
    {
        obj = pltAllocInterrupt (vector, PLT_INT_HWINT);
        obj->hwInt = hwInt;
        // Enable it if not an internally managed interrupt
        if (!hwInt->flags & PLT_HWINT_INTERNAL)
            platform->intCtrl->enableInterrupt (CpuGetCcb(), hwInt);
    }
    CpuEnable();
    return obj;
}

// Allocates a hardware interrupt
NkHwInterrupt_t* PltAllocHwInterrupt()
{
    return MmCacheAlloc (nkHwIntCache);
}

// Connects an interrupt to hardware controller
int PltConnectInterrupt (NkHwInterrupt_t* hwInt)
{
    // Validate it
    if (hwInt->ipl > PLT_IPL_TIMER)
        return -1;
    if (hwInt->ipl == 0)
        ++hwInt->ipl;    // Default to lowest priority
    CpuDisable();
    int vector = platform->intCtrl->connectInterrupt (CpuGetCcb(), hwInt);
    CpuEnable();
    NkLogDebug ("nexke: connected interrupt %u to vector %d\n", hwInt->gsi, vector);
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
        NkLogDebug ("nexke: disconnected interrupt %u\n", intObj->hwInt->gsi);
        // Disconnect and disable
        platform->intCtrl->disconnectInterrupt (CpuGetCcb(), intObj->hwInt);
    }
    nkIntTable[intObj->vector] = NULL;
    if (intObj->type == PLT_INT_HWINT)
        MmCacheFree (nkHwIntCache, intObj->hwInt);
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
    nkHwIntCache = MmCacheCreate (sizeof (NkHwInterrupt_t), NULL, NULL);
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
    ++ccb->intCount;
    // Grab the interrupt object
    NkInterrupt_t* intObj = nkIntTable[CPU_CTX_INTNUM (context)];
    if (!intObj)
    {
        // Unhandled interrupt, that's a bad trap
        PltBadTrap (context, "unhandled interrupt %#X", CPU_CTX_INTNUM (context));
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
        ccb->curIpl = intObj->hwInt->ipl;
        // Check if this interrupt is spurious
        if (!platform->intCtrl->beginInterrupt (ccb, intObj->hwInt))
        {
            if (intObj->hwInt->flags & PLT_HWINT_SPURIOUS)
            {
                // This interrupt is spurious. Increase counter and return
                ++ccb->spuriousInts;
            }
        }
        else
        {
            // Re-enable interrupts
            CpuEnable();
            // Handle
            ++intObj->callCount;
            intObj->hwInt->handler (intObj, context);
            CpuDisable();
            // End the interrupt
            platform->intCtrl->endInterrupt (ccb, intObj->hwInt);
        }
        // Restore IPL
        ccb->curIpl = oldIpl;
    }
    else
        assert (!"Invalid interrupt type");
}

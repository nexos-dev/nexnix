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

// Chain for all internal interrupts
static PltHwIntChain_t internalChain = {0};

// Chain helpers

static inline PltHwIntChain_t* pltGetChain (uint32_t gsi)
{
    if (gsi == PLT_GSI_INTERNAL)
        return &internalChain;
    return &platform->intCtrl->lineMap[gsi];
}

static inline size_t pltGetLineMapSize()
{
    return platform->intCtrl->mapEntries;
}

static inline void pltInitChain (PltHwIntChain_t* chain)
{
    NkListInit (&chain->list);
    chain->maskCount = 0;
    chain->chainLen = 0;
    chain->noRemap = false;
}

// Interrupt allocation
static inline NkInterrupt_t* pltAllocInterrupt (int vector, int type)
{
    // Ensure vector is free
    if (nkIntTable[vector])
        return NULL;    // Interrupt is in use
    NkInterrupt_t* obj = (NkInterrupt_t*) MmCacheAlloc (nkIntCache);
    if (!obj)
        NkPanic ("nexke: out of memory");
    memset (obj, 0, sizeof (NkInterrupt_t));
    // Initialize object
    obj->callCount = 0;
    obj->type = type;
    obj->vector = vector;
    // Insert in table
    nkIntTable[vector] = obj;
    return obj;
}

// Adds interrupt to chain
static inline void pltChainInterrupt (NkInterrupt_t* obj, NkHwInterrupt_t* hwInt)
{
    assert (obj->type == PLT_INT_HWINT);
    assert (hwInt->gsi == PLT_GSI_INTERNAL || hwInt->gsi < pltGetLineMapSize());
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    if (!chain->chainLen)
        pltInitChain (chain);
    // Link it
    NkListAddFront (&chain->list, &hwInt->link);
    ++chain->chainLen;
    // Check if we need to mark it as chained
    if (chain->chainLen > 1)
    {
        hwInt->flags |= PLT_HWINT_CHAINED;
        if (chain->chainLen == 2)
        {
            // The chain just started so we need to set the bit in current chained item
            NkHwInterrupt_t* hwInt2 = LINK_CONTAINER (hwInt->link.next, NkHwInterrupt_t, link);
            hwInt2->flags |= PLT_HWINT_CHAINED;
        }
    }
}

// Removes interrupt from chain
static inline void pltUnchainInterrupt (NkInterrupt_t* obj, NkHwInterrupt_t* hwInt)
{
    assert (obj->type == PLT_INT_HWINT);
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    assert (hwInt->gsi == PLT_GSI_INTERNAL || hwInt->gsi < pltGetLineMapSize());
    // Unlink it
    NkListRemove (&chain->list, &hwInt->link);
    --chain->chainLen;
    if (chain->chainLen == 1)
    {
        // Unmark it as chained
        NkHwInterrupt_t* headInt =
            LINK_CONTAINER (NkListFront (&chain->list), NkHwInterrupt_t, link);
        headInt->flags &= ~(PLT_HWINT_CHAINED);
    }
}

// Checks if two hardware interrupts are compatible
bool PltAreIntsCompatible (NkHwInterrupt_t* int1, NkHwInterrupt_t* int2)
{
    if (int1->mode != int2->mode ||
        (int1->flags & PLT_HWINT_ACTIVE_LOW != int2->flags & PLT_HWINT_ACTIVE_LOW))
    {
        return false;    // Can't do it
    }
    return true;
}

// Retrieves interrupt obejct from table
NkInterrupt_t* PltGetInterrupt (int vector)
{
    assert (vector < NK_MAX_INTS);
    return nkIntTable[vector];
}

// Installs an exception handler
NkInterrupt_t* PltInstallExec (int vector, PltIntHandler hndlr)
{
    assert (vector < NK_MAX_INTS);
    if (vector > CPU_BASE_HWINT)
        return NULL;    // Can't cross into hardware vectors
    CpuDisable();
    NkInterrupt_t* obj = pltAllocInterrupt (vector, PLT_INT_EXEC);
    obj->handler = hndlr;
    CpuEnable();
    return obj;
}

// Installs a service handler
NkInterrupt_t* PltInstallSvc (int vector, PltIntHandler hndlr)
{
    assert (vector < NK_MAX_INTS);
    if (vector > CPU_BASE_HWINT)
        return NULL;    // Can't cross into hardware vectors
    CpuDisable();
    NkInterrupt_t* obj = pltAllocInterrupt (vector, PLT_INT_SVC);
    obj->handler = hndlr;
    CpuEnable();
    return obj;
}

// Installs a hardware interrupt
NkInterrupt_t* PltInstallInterrupt (int vector, NkHwInterrupt_t* hwInt)
{
    assert (vector < NK_MAX_INTS);
    CpuDisable();
    // Check if interrupt is installed
    NkInterrupt_t* obj = nkIntTable[vector];
    if (obj)
    {
        // Make sure this is allowed
        if (hwInt->flags & PLT_HWINT_NON_CHAINABLE)
            return NULL;
        pltChainInterrupt (obj, hwInt);
    }
    else
    {
        // Allocate a new interrupt
        obj = pltAllocInterrupt (vector, PLT_INT_HWINT);
        // Get chain
        PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
        obj->intChain = chain;
        // Start chain
        pltChainInterrupt (obj, hwInt);
        // Enable it if not an internally managed interrupt
        if (!(hwInt->flags & PLT_HWINT_INTERNAL))
            platform->intCtrl->enableInterrupt (CpuGetCcb(), hwInt);
    }
    CpuEnable();
    return obj;
}

// Remaps hardware interrupts on specified object to a new vector and IPL
// Requires input to be a hardware interrupt object, and returns the new interrupt
// Called with interrupts disabled
NkInterrupt_t* PltRemapInterrupt (NkInterrupt_t* oldInt, int newVector, ipl_t newIpl)
{
    assert (newVector < NK_MAX_INTS);
    assert (oldInt->type == PLT_INT_HWINT);
    // Allocate the new vector
    NkInterrupt_t* newInt = pltAllocInterrupt (newVector, PLT_INT_HWINT);
    if (!newInt)
        return NULL;
    // Move the chain
    newInt->intChain = oldInt->intChain;
    // Change vector and IPL on all the interrupts
    NkLink_t* iter = NkListFront (&oldInt->intChain->list);
    while (iter)
    {
        NkHwInterrupt_t* curInt = LINK_CONTAINER (iter, NkHwInterrupt_t, link);
        curInt->vector = newVector;
        curInt->ipl = newIpl;
        iter = NkListIterate (iter);
    }
    // Uninstall the old interrupt
    PltUninstallInterrupt (oldInt);
    return newInt;
}

// Allocates a hardware interrupt
NkHwInterrupt_t* PltAllocHwInterrupt()
{
    NkHwInterrupt_t* intObj = MmCacheAlloc (nkHwIntCache);
    memset (intObj, 0, sizeof (NkHwInterrupt_t));
    return intObj;
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
    return vector;
}

// Disconnects interrupt from hardware controller
void PltDisconnectInterrupt (NkHwInterrupt_t* hwInt)
{
    // Unchain and then disconnect it
    CpuDisable();
    pltUnchainInterrupt (PltGetInterrupt (hwInt->vector), hwInt);
    // NOTE: if interrupt is not chained, disconnect will disable it for us
    platform->intCtrl->disconnectInterrupt (CpuGetCcb(), hwInt);
    CpuEnable();
}

// Enables an interrupt
void PltEnableInterrupt (NkHwInterrupt_t* hwInt)
{
    CpuDisable();
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    hwInt->flags &= ~(PLT_HWINT_MASKED);
    if (chain->maskCount)
        --chain->maskCount;
    else
        platform->intCtrl->enableInterrupt (CpuGetCcb(), hwInt);
    CpuEnable();
}

// Disables an interrupt
void PltDisableInterrupt (NkHwInterrupt_t* hwInt)
{
    CpuDisable();
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    hwInt->flags |= PLT_HWINT_MASKED;
    if (!chain->maskCount)
        platform->intCtrl->disableInterrupt (CpuGetCcb(), hwInt);
    ++chain->maskCount;
    CpuEnable();
}

// Uninstalls an interrupt handler
void PltUninstallInterrupt (NkInterrupt_t* intObj)
{
    if (!nkIntTable[intObj->vector])
        NkPanic ("nexke: can't uninstall non-existant interrupt");
    CpuDisable();
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
        ccb->intActive = true;
        //  Check if this interrupt is spurious
        if (!platform->intCtrl->beginInterrupt (ccb, CPU_CTX_INTNUM (context)))
        {
            // This interrupt is spurious. Increase counter and return
            ++ccb->spuriousInts;
        }
        else
        {
            // Re-enable interrupts
            CpuEnable();
            // Handle
            ++intObj->callCount;
            // Loop over the entire chain, trying to find a interrupt that can handle it
            NkLink_t* iter = NkListFront (&intObj->intChain->list);
            NkHwInterrupt_t* curInt = LINK_CONTAINER (iter, NkHwInterrupt_t, link);
            ipl_t oldIpl = ccb->curIpl;
            ccb->curIpl = curInt->ipl;    // Set IPL
            while (iter)
            {
                if (!(curInt->flags & PLT_HWINT_MASKED) && curInt->handler (intObj, context))
                    break;    // Found one
                iter = NkListIterate (iter);
                curInt = LINK_CONTAINER (iter, NkHwInterrupt_t, link);
            }
            ccb->curIpl = oldIpl;    // Restore IPL
            CpuDisable();
            // End the interrupt
            platform->intCtrl->endInterrupt (ccb, CPU_CTX_INTNUM (context));
        }
        ccb->intActive = false;
    }
    else
        assert (!"Invalid interrupt type");
}

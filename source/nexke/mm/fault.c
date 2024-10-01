/*
    fault.c - contains page fault handler
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
#include <nexke/mm.h>
#include <nexke/nexke.h>

// Fault entry point
bool MmPageFault (uintptr_t vaddr, int prot)
{
    // Get the address page aligned
    vaddr = CpuPageAlignDown (vaddr);
    // Get the address space
    MmSpace_t* space = MmGetCurrentSpace();
    // If this is a kernel fault, than change it to the kernel space
    if (prot & MUL_PAGE_KE)
        space = MmGetKernelSpace();
    // Find the address space entry for this address
    MmSpaceEntry_t* entry = MmFindSpaceEntry (space, vaddr);
    if (!entry)
        return false;    // Page just doesn't exist
    assert (entry->obj);
    MmPage_t* outPage = NULL;
    bool res = MmPageFaultIn (entry->obj, vaddr - entry->vaddr, &prot, &outPage);
    if (!res)
        return false;    // Page could not be faulted in
    // Add this page to MUL
    MmMulMapPage (space, vaddr, outPage, prot);
    return true;
}

// Brings a page into memory during a page fault
bool MmPageFaultIn (MmObject_t* obj, size_t offset, int* prot, MmPage_t** outPage)
{
    // So this is basically the heart of the memory manager. We split this up into a couple phases
    // First, we find the page that is supposed to back this object/offset.
    // Second, we will figure out wheter this is a protection or access violation
    // If it is a protection violation, we will attempt to fix it or fail
    // If it is a access violation, we will bring the page into memory
    MmPage_t* page = NULL;    // The page itself
    // Attempt to lookup the page in the object
    page = MmLookupPage (&obj->pageList, offset);
    if (!page)
    {
        // This page is not resident in memory, allocate a page and do a page in
        page = MmAllocPage();
        if (!page)
            NkPanicOom();
        MmAddPage (&obj->pageList, page, offset);
        if (!MmBackendPageIn (obj, offset, page))
            NkPanic ("nexke: page in error\n");
    }
    // We have a page, now we need to figure out what we need to do (if anything)
    // First figure out if this is a access or protection violation
    int err = *prot;
    if (err & MUL_PAGE_P)
    {
        // Set flags and page and return because we were successful
        *prot = obj->perm;
        *outPage = page;
        return true;
    }
    return false;
}
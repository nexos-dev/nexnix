/*
    object.c - contains memory object management
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

#include "backends.h"
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

static SlabCache_t* mmObjCache = NULL;    // Contains object slab cache

// Initializes object system
void MmInitObject()
{
    mmObjCache = MmCacheCreate (sizeof (MmObject_t), "MmObject_t", 0, 0);
    if (!mmObjCache)
        NkPanicOom();
}

// Creates a new memory object
MmObject_t* MmCreateObject (size_t pages, int backend, int perm)
{
    MmObject_t* obj = MmCacheAlloc (mmObjCache);
    if (!obj)
        NkPanicOom();
    obj->backend = backend;
    obj->perm = perm;
    obj->count = pages;
    NkListInit (&obj->pageList);
    obj->refCount = 1;
    if (backend > MM_BACKEND_MAX)
    {
        MmCacheFree (mmObjCache, obj);
        return NULL;
    }
    obj->backendTab = backends[backend];
    // Tell backend
    MmBackendInit (obj);
    return obj;
}

// References a memory object
void MmRefObject (MmObject_t* object)
{
    NkSpinLock (&object->lock);
    ++object->refCount;
    NkSpinUnlock (&object->lock);
}

// Dereferences a memory object
void MmDeRefObject (MmObject_t* object)
{
    NkSpinLock (&object->lock);
    --object->refCount;
    if (!object->refCount)
    {
        // Destroy all pages in object
        NkLink_t* iter = NkListFront (&object->pageList);
        while (iter)
        {
            MmPage_t* page = LINK_CONTAINER (iter, MmPage_t, objLink);
            NkSpinLock (&page->lock);
            MmRemovePage (page);
            // Free it
            MmFreePage (page);
            iter = NkListIterate (iter);
            NkSpinUnlock (&page->lock);
        }
        MmBackendDestroy (object);
    }
    NkSpinUnlock (&object->lock);
}

// Applies new permissions to object
void MmProtectObject (MmObject_t* object, int newPerm)
{
}

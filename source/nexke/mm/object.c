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
    mmObjCache = MmCacheCreate (sizeof (MmObject_t), NULL, NULL);
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
    memset (&obj->pageList, 0, sizeof (MmPageList_t));
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
    ++object->refCount;
}

// Dereferences a memory object
void MmDeRefObject (MmObject_t* object)
{
    --object->refCount;
    if (!object->refCount)
    {
        MmClearPageList (&object->pageList);
        MmBackendDestroy (object);
    }
}

// Applies new permissions to object
void MmProtectObject (MmObject_t* object, int newPerm)
{
}

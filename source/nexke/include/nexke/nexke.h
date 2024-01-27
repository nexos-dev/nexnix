/*
    nexke.h - contains main kernel functions
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

#ifndef _NEXKE_H
#define _NEXKE_H

#include <nexke/cpu.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <version.h>

// Initialization routines

// Initializes phase 1 memory management
void MmInitPhase1();

// Initializes phase 2 memory management
void MmInitPhase2();

// Initializes FB console
void NkFbConsInit();

// Argument processing

// Gets specified argument value
// Returns NULL if argument is non-existant, "" if argument is empty, value otherwose
const char* NkReadArg (const char* arg);

// Log functions

// Initializes log
void NkLogInit();

// Logging functions
void NkLogInfo (const char* fmt, ...);
void NkLogDebug (const char* fmt, ...);
void NkLogWarning (const char* fmt, ...);
void NkLogError (const char* fmt, ...);
void __attribute__ ((noreturn)) NkPanic (const char* fmt, ...);

// Slab related structures / functions

// Constructor / destructor type
typedef void (*SlabObjConstruct) (void* obj);    // Initialize obj
typedef void (*SlabObjDestruct) (void* obj);     // Destroy obj

typedef struct _slab Slab_t;

// Slab cache
typedef struct _slabcache
{
    // Slab pointers
    Slab_t* emptySlabs;      // Pointer to empty slabs
    Slab_t* partialSlabs;    // Pointer to partial slabs
    Slab_t* fullSlabs;       // Pointer to full slabs
    // Stats
    int numEmpty;    // Number of empty slabs. Used to know when to free a slab to the PMM
    int numObjs;     // Number of currently allocated objects in this cache
    // Typing info
    size_t objSz;       // Size of an object
    size_t objAlign;    // Minimum alignment of object. This is the size rounded to nearest multiple
                        // of 8
    SlabObjConstruct constructor;    // Object constructor
    SlabObjDestruct destructor;      // Object destructor
} SlabCache_t;

// Creates a new slab cache
SlabCache_t* MmCacheCreate (size_t objSz, SlabObjConstruct constuctor, SlabObjDestruct destructor);

// Destroys a slab cache
void MmCacheDestroy (SlabCache_t* cache);

// Allocates an object from a slab cache
void* MmCacheAlloc (SlabCache_t* cache);

// Frees an object back to slab cache
void MmCacheFree (SlabCache_t* cache, void* obj);

#endif

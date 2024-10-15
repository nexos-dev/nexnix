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
#include <nexke/list.h>
#include <nexke/types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <version.h>

// Initialization routines

// Initializes FB console
void NkFbConsInit();

// Argument processing

// Gets specified argument value
// Returns NULL if argument is non-existant, "" if argument is empty, value otherwose
const char* NkReadArg (const char* arg);

// Helper function to compute checksums
bool NkVerifyChecksum (uint8_t* buf, size_t len);

// Log functions

// Loglevels
#define NK_LOGLEVEL_EMERGENCY 1
#define NK_LOGLEVEL_CRITICAL  2
#define NK_LOGLEVEL_ERROR     3
#define NK_LOGLEVEL_WARNING   4
#define NK_LOGLEVEL_NOTICE    5
#define NK_LOGLEVEL_INFO      6
#define NK_LOGLEVEL_DEBUG     7

// Initializes log
void NkLogInit();

// Logging functions
void NkLogInfo (const char* fmt, ...);
void NkLogDebug (const char* fmt, ...);
void NkLogWarning (const char* fmt, ...);
void NkLogError (const char* fmt, ...);
void __attribute__ ((noreturn)) NkPanic (const char* fmt, ...);
void NkLogMessage (const char* fmt, int level, va_list ap);

// Short hand for OOM condition
#define NkPanicOom() (NkPanic ("nexke: out of memory"))

// Slab related structures / functions

typedef struct _slab Slab_t;

// Slab cache
typedef struct _slabcache
{
    // General info
    const char* name;    // Name of this cache
    int flags;           // Cache flags
    // Slab pointers
    NkList_t emptySlabs;      // Pointer to empty slabs
    NkList_t partialSlabs;    // Pointer to partial slabs
    NkList_t fullSlabs;       // Pointer to full slabs
    // Stats
    int numFull;       // Number of full slabs
    int numPartial;    // Number of partial slabs
    int numEmpty;      // Number of empty slabs. Used to know when to free a slab to the PMM
    int numObjs;       // Number of currently allocated objects in this cache
    // Typing info
    size_t objSz;     // Size of an object, aligned to an 8 byte boundary
    size_t align;     // Alignment of each object. Defaults to 8
    size_t maxObj;    // Max object in one slab
    // Slab sizing
    size_t slabSz;    // The size of one slab in pages
    // Coloring info
    size_t numColors;    // The total number of colors
    size_t colorAdj;     // Equals alignment
    size_t curColor;     // Current color
    NkLink_t link;       // Link in cache list
} SlabCache_t;

#define SLAB_CACHE_EXT_SLAB    (1 << 0)
#define SLAB_CACHE_DEMAND_PAGE (1 << 1)

// Creates a new slab cache
SlabCache_t* MmCacheCreate (size_t objSz, const char* name, size_t align, int flags);

// Destroys a slab cache
void MmCacheDestroy (SlabCache_t* cache);

// Allocates an object from a slab cache
void* MmCacheAlloc (SlabCache_t* cache);

// Frees an object back to slab cache
void MmCacheFree (SlabCache_t* cache, void* obj);

// Dumps the state of the slab allocator
void MmSlabDump();

// Malloc/free
void* kmalloc (size_t sz);

void kfree (void* ptr, size_t sz);

// Timer interface

// Callback type
typedef void (*NkTimeCallback) (NkTimeEvent_t*, void*);

// Timer event structure
typedef struct _timeevt
{
    uint64_t deadline;          // Deadline for this event
                                // NOTE: this is internal, as this is in internal clock ticks
    NkTimeCallback callback;    // Callback function
    void* arg;                  // Argument to pass to callback
    bool inUse;                 // Event current registered
    NkLink_t link;
} NkTimeEvent_t;

// Initializes timing subsystem
void NkInitTime();

// Registers a time event
void NkTimeRegEvent (NkTimeEvent_t*, uint64_t delta, NkTimeCallback callback, void* arg);

// Deregisters a time event
void NkTimeDeRegEvent (NkTimeEvent_t* event);

// Allocates a timer event
NkTimeEvent_t* NkTimeNewEvent();

// Frees a timer event
void NkTimeFreeEvent (NkTimeEvent_t* event);

// Resource interface

// Hash table of chunks size
#define NK_NUM_CHUNK_HASH 256

typedef struct _resarena
{
    const char* name;                         // Name of arena
    NkList_t chunks;                          // List of chunks in this arena
    size_t numChunks;                         // Number of chunks
    id_t minId;                               // Minimum ID
    id_t maxId;                               // The max resource ID that can come out of here
    NkList_t chunkHash[NK_NUM_CHUNK_HASH];    // hash table of chunks
    NkLink_t link;                            // Link in arena list
} NkResArena_t;

// Creates a resource arena
NkResArena_t* NkCreateResource (const char* name, id_t minId, id_t maxId);

// Destroys a resource arena
void NkDestroyResource (NkResArena_t* arena);

// Allocates a resource
id_t NkAllocResource (NkResArena_t* arena);

// Frees a resource
void NkFreeResource (NkResArena_t* arena, id_t res);

// Initializes resource system
void NkInitResource();

#endif

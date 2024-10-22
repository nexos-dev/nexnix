/*
    resource.c - contains resource allocator for nexke
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
#include <string.h>

// nexke's resource allocator is very simple in design
// It allocates out integer IDs used to identify resources
// Basically we have multiple arenas, each arena is for a different resource type
// Each arena is split into chunks, and chunks are sorted by the number of free resources
// in them. Each chunk contains a 64-bit bitmap specifying which resource IDs in said chunk are free
// or allocated, but also caches up to 6 IDs in an array for instant access
// That's the gist of how this works

// Chunk structure

#define NK_CHUNK_MAX_FREE_CACHE 6    // Chunk free cache size

typedef struct _reschunk
{
    // Base info
    size_t numFree;    // Number of free IDs
    int type;          // Either ranged or mapped
                       // If ranged, map is ignored and this specifies a range of free IDs
    // Mapping info
    uint64_t allocMap;    // Map of free entries in this chunk
    id_t baseId;          // First ID in chunk
    size_t lastId;        // Last ID in chunk
    // Free cache info
    id_t freeCache[NK_CHUNK_MAX_FREE_CACHE];    // Cache of free IDs, in relative ID
    size_t curCacheId;                          // Current cache index
    spinlock_t chunkLock;                       // Chunk lock
    NkLink_t link;                              // Link to next chunk
    NkLink_t hashLink;                          // Hash table link
} NkResChunk_t;

// Chunk types
#define NK_CHUNK_RANGED 0
#define NK_CHUNK_MAPPED 1

// ID multiple
#define NK_ID_MULTIPLE 64

// Cache of arenas
static SlabCache_t* arenaCache = NULL;

// Cache of chunks
static SlabCache_t* chunkCache = NULL;

// List of arenas
static NkList_t arenas = {0};
static spinlock_t arenasLock = 0;

// Aligns an ID
static FORCEINLINE id_t nkAlignId (id_t id, size_t align)
{
    // Align it
    id += align - 1;
    id &= ~(align - 1);
    return id;
}

// Aligns an ID down
static FORCEINLINE id_t nkAlignIdDown (id_t id, size_t align)
{
    // Align it
    id &= ~(align - 1);
    return id;
}

// Adds to chunk hash table
static FORCEINLINE void nkHashChunk (NkResArena_t* arena, NkResChunk_t* chunk, id_t baseId)
{
    size_t idx = baseId % NK_NUM_CHUNK_HASH;
    NkSpinLock (&arena->hashLock);
    NkListAddFront (&arena->chunkHash[idx], &chunk->link);
    NkSpinUnlock (&arena->hashLock);
}

// Gets a hashed chunk
// Returns chunk locked
static FORCEINLINE NkResChunk_t* nkGetChunk (NkResArena_t* arena, id_t baseId)
{
    size_t idx = baseId % NK_NUM_CHUNK_HASH;
    NkLink_t* iter = NkListFront (&arena->chunkHash[idx]);
    NkResChunk_t* chunk = NULL;
    NkSpinLock (&arena->hashLock);
    while (iter)
    {
        chunk = LINK_CONTAINER (iter, NkResChunk_t, link);
        if (chunk->baseId == baseId)
        {
            NkSpinLock (&chunk->chunkLock);
            break;
        }
        iter = NkListIterate (iter);
    }
    NkSpinUnlock (&arena->hashLock);
    return chunk;
}

#define CHUNK_MAYBE_LOCK(chunk) \
    if (iter)                   \
        NkSpinLock (&(chunk)->chunkLock);
#define CHUNK_MAYBE_UNLOCK(chunk) \
    if (iter)                     \
        NkSpinUnlock (&(chunk)->chunkLock);

// Sorts a chunk to it's appropriate spot
// Called with chunk and list locked
static FORCEINLINE void nkSortChunk (NkResArena_t* arena, NkResChunk_t* chunk)
{
    // Find where this chunk should go
    // To do this, we go into a loop and check the left and right chunk
    // We keep doing this until we find our spot
    NkLink_t* iter = chunk->link.prev;
    while (1)
    {
        // Get the chunk
        NkResChunk_t* leftChunk = LINK_CONTAINER (iter, NkResChunk_t, link);
        CHUNK_MAYBE_LOCK (leftChunk);
        // If we've reached the end or the left has more free than us place us here
        if (!iter || leftChunk->numFree >= chunk->numFree)
        {
            // Found spot, check if we need to move it
            if (chunk->link.prev == iter)
            {
                CHUNK_MAYBE_UNLOCK (leftChunk);
                break;    // Nothing to move
            }
            // Move it to this spot
            NkListRemove (&arena->chunks, &chunk->link);
            if (!iter)
                NkListAddFront (&arena->chunks, &chunk->link);
            else
                NkListAddBefore (&arena->chunks, iter, &chunk->link);
            CHUNK_MAYBE_UNLOCK (leftChunk);
            break;
        }
        CHUNK_MAYBE_UNLOCK (leftChunk);
        iter = iter->prev;
    }
    // Now move to right
    iter = chunk->link.next;
    while (1)
    {
        // Get the chunk
        NkResChunk_t* rightChunk = LINK_CONTAINER (iter, NkResChunk_t, link);
        CHUNK_MAYBE_LOCK (rightChunk);
        // If we've reached the end or the left has more free than us place us here
        if (!iter || rightChunk->numFree <= chunk->numFree)
        {
            // Found spot, check if we need to move it
            if (chunk->link.next == iter)
            {
                CHUNK_MAYBE_UNLOCK (rightChunk);
                break;    // Nothing to move
            }
            // Move it to this spot
            NkListRemove (&arena->chunks, &chunk->link);
            if (!iter)
                NkListAddBack (&arena->chunks, &chunk->link);
            else
                NkListAdd (&arena->chunks, iter, &chunk->link);
            CHUNK_MAYBE_UNLOCK (rightChunk);
            break;
        }
        CHUNK_MAYBE_UNLOCK (rightChunk);
        iter = NkListIterate (iter);
    }
}

// Allocates from bitmap
static FORCEINLINE id_t nkSearchMap (NkResChunk_t* chunk)
{
    for (int i = 0; i < 64; ++i)
    {
        if (!(chunk->allocMap & (1 << i)))
        {
            chunk->allocMap |= (1 << i);
            return i;
        }
    }
    return -1;
}

// Allocates a resource
id_t NkAllocResource (NkResArena_t* arena)
{
    // Grab the first chunk
    NkLink_t* link = NkListFront (&arena->chunks);
    if (!link)
        return -1;    // No free IDs
    NkResChunk_t* chunk = LINK_CONTAINER (link, NkResChunk_t, link);
    NkSpinLock (&chunk->chunkLock);
    if (chunk->numFree == 0)
        return -1;
    // Check if this is ranged or mapped
    id_t id = -1;
    if (chunk->type == NK_CHUNK_RANGED)
    {
        // Grab an id
        // We don't split it to another chunk until we free it
        // As this is the performance hot spot, so we try to minimize the work done here
        id = chunk->baseId;
        ++chunk->baseId;
        if (chunk->baseId >= chunk->lastId)
        {
            NkSpinLock (&arena->listLock);
            NkListRemove (&arena->chunks, &chunk->link);    // Remove chunk from list, list is empty
            NkSpinUnlock (&arena->listLock);
        }
    }
    else
    {
        // First check free list, and if we can't check bitmap
        if (chunk->curCacheId < NK_CHUNK_MAX_FREE_CACHE)
        {
            id = chunk->freeCache[chunk->curCacheId] + chunk->baseId;    // Get it
            if (id != -1)
                ++chunk->curCacheId;    // Increment it
        }
        if (id != -1)    // Check if that worked
        {
            // Get our ID
            id = nkSearchMap (chunk) + chunk->baseId;
            assert (id != -1);
            // Fill up free cache from bitmap
            for (int i = 0; i < NK_CHUNK_MAX_FREE_CACHE; ++i)
            {
                chunk->freeCache[i] = nkSearchMap (chunk);    // Grab the ID
                if (chunk->freeCache[i] == -1)
                    break;    // Out of IDs
                chunk->curCacheId = 0;
            }
        }
    }
    --chunk->numFree;
    // Sort this chunk
    NkSpinLock (&arena->listLock);
    nkSortChunk (arena, chunk);
    NkSpinUnlock (&arena->listLock);
    NkSpinUnlock (&chunk->chunkLock);
    return id;
}

// Frees a resource
void NkFreeResource (NkResArena_t* arena, id_t res)
{
    // First grab the chunk, if there is one
    id_t baseId = nkAlignIdDown (res, 64);
    NkResChunk_t* chunk = nkGetChunk (arena, baseId);
    if (!chunk)
    {
        // Create a new one for this ID
        chunk = MmCacheAlloc (chunkCache);
        if (!chunk)
            NkPanicOom();
        memset (chunk, 0, sizeof (NkResChunk_t));
        // Set map to all ones since we don't know what's free and what's not
        chunk->allocMap = (uint64_t) UINT64_MAX;
        chunk->baseId = baseId;
        chunk->lastId = baseId + 63;
        // Free what we know is free
        chunk->numFree = 1;
        chunk->allocMap &= ~(1 << (res - baseId));
        // Setup free cache
        chunk->freeCache[0] = -1;
        chunk->curCacheId = NK_CHUNK_MAX_FREE_CACHE;
        // Add to lists
        NkSpinLock (&chunk->chunkLock);
        NkSpinLock (&arena->listLock);
        NkListAddFront (&arena->chunks, &chunk->link);
        nkSortChunk (arena, chunk);
        NkSpinUnlock (&arena->listLock);
        nkHashChunk (arena, chunk, baseId);
    }
    else
    {
        // Unset the bit
        size_t idx = res - baseId;
        chunk->allocMap &= ~(1 << idx);
        ++chunk->numFree;
        // TODO: add ID to free list if beneficial
    }
    NkSpinUnlock (&chunk->chunkLock);
}

// Creates a resource arena
NkResArena_t* NkCreateResource (const char* name, id_t minId, id_t maxId)
{
    // Allocate the arena
    NkResArena_t* arena = MmCacheAlloc (arenaCache);
    if (!arena)
        return NULL;
    memset (arena, 0, sizeof (NkResArena_t));
    arena->name = name;
    arena->numChunks = 1;    // One chunk to start
    // Round max ID to multiple of 64
    maxId = nkAlignId (maxId + 1, NK_ID_MULTIPLE) - 1;
    arena->minId = minId;
    arena->maxId = maxId;
    NkListInit (&arena->chunks);
    // Create first chunk
    NkResChunk_t* chunk = MmCacheAlloc (chunkCache);
    if (!chunk)
        return NULL;
    memset (chunk, 0, sizeof (NkResChunk_t));
    chunk->numFree = maxId + 1;
    chunk->type = NK_CHUNK_RANGED;
    chunk->baseId = arena->minId;
    chunk->lastId = chunk->numFree - 1;
    // Add chunk
    NkListAddFront (&arena->chunks, &chunk->link);
    // Add arena
    NkSpinLock (&arenasLock);
    NkListAddFront (&arenas, &arena->link);
    NkSpinUnlock (&arenasLock);
    return arena;
}

// Destroys a resource arena
void NkDestroyResource (NkResArena_t* arena)
{
    // Go through every chunk
    NkLink_t* iter = NkListFront (&arena->chunks);
    while (iter)
    {
        NkResChunk_t* chunk = LINK_CONTAINER (iter, NkResChunk_t, link);
        MmCacheFree (chunkCache, chunk);
        iter = NkListIterate (iter);
    }
    // Remove from list
    NkSpinLock (&arenasLock);
    NkListRemove (&arenas, &arena->link);
    NkSpinUnlock (&arenasLock);
    MmCacheFree (arenaCache, arena);
}

// Initializes resource system
void NkInitResource()
{
    // Create caches
    arenaCache = MmCacheCreate (sizeof (NkResArena_t), "NkResArena_t", 0, 0);
    chunkCache = MmCacheCreate (sizeof (NkResChunk_t), "NkResChunk_t", 0, 0);
}

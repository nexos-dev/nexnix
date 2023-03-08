/*
    memmgr.c - contains dynamic memory allocator
    Copyright 2023 The NexNix Project

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

#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Headers
typedef struct _memblock
{
    uint32_t magic;
    uint32_t size;
    bool isFree;
    struct _mempage* page;
    struct _memblock* prev;
    struct _memblock* next;
} __attribute__ ((packed)) memBlock_t;

typedef struct _mempage
{
    uint32_t magic;           // Magic size
    uint32_t freeSize;        // Free number of bytes
    memBlock_t* blockList;    // List of blocks
    struct _mempage* next;
    struct _mempage* prev;
} __attribute__ ((packed)) memPage_t;

#define MEM_BLOCK_MAGIC 0xF9125937

#define MEM_PG_BLOCK_OFFSET   32
#define MEM_BLOCK_DATA_OFFSET 32

#define MEM_BLOCK_ALIGN 16

#define MEM_BLOCK_SIZE_END(block) (size_t*) ((uintptr_t) (block) + (block)->size - 4)

static memPage_t* pageList = NULL;

void memBlockInit (memPage_t* page, memBlock_t* block)
{
    block->magic = MEM_BLOCK_MAGIC;
    block->next = NULL;
    block->prev = NULL;
    block->size = NEXBOOT_CPU_PAGE_SIZE;
    block->page = pageList;
    block->isFree = true;
    size_t* sizePtr = MEM_BLOCK_SIZE_END (block);
    *sizePtr = NEXBOOT_CPU_PAGE_SIZE;
}

void memPageInit (memPage_t* page)
{
    page->next = NULL;
    page->prev = NULL;
    page->magic = MEM_BLOCK_MAGIC;
    page->freeSize = NEXBOOT_CPU_PAGE_SIZE;
}

void NbMemInit()
{
    NbFwMemDetect();    // Get memory map
    int size = 0;
    NbMemEntry_t* memmap = NbGetMemMap (&size);
    uintmax_t memSz = 0;
    for (int i = 0; i < size; ++i)
    {
        // Get memory size
        if (memmap[i].type == NEXBOOT_MEM_FREE ||
            memmap[i].type == NEXBOOT_MEM_ACPI_RECLAIM ||
            memmap[i].type == NEXBOOT_MEM_BOOT_RECLAIM)
        {
            memSz += memmap[i].sz;
        }
    }
    // Convert to MiB
    memSz /= (uint64_t) (1024 * 1024);
    // Ensure we have enough memory
    if (memSz < NEXBOOT_MIN_MEM)
    {
        // That's an error
        NbLogMessageEarly ("Error: NexNix requires at least %d MiB of memory. Only "
                           "%d MiB were detected",
                           NEXBOOT_LOGLEVEL_ERROR,
                           NEXBOOT_MIN_MEM,
                           memSz);
        NbCrash();
    }
    // Allocate initial page
    uintptr_t initPage = NbFwAllocPage();
    pageList = (memPage_t*) initPage;
    memPageInit (pageList);
    // Prepare a block
    memBlock_t* block = (memBlock_t*) (initPage + MEM_PG_BLOCK_OFFSET);
    memBlockInit (pageList, block);
    pageList->blockList = block;
}

void __attribute__ ((noreturn)) memCorrupted()
{
    NbLogMessageEarly ("FATAL ERROR: Memory corruption detected",
                       NEXBOOT_LOGLEVEL_EMERGENCY);
    NbCrash();
    __builtin_unreachable();
}

static void* allocBlockInPage (memPage_t* pg, size_t sz)
{
    memBlock_t* curBlock = pg->blockList;
    while (curBlock)
    {
        // Check if we fit in this block
        if (curBlock->magic != MEM_BLOCK_MAGIC)
            memCorrupted();
        if (curBlock->size >= sz)
        {
            // Use this block
            break;
        }
        curBlock = curBlock->next;
    }
    if (!curBlock)
        return NULL;    // No large enough block
    // Decrease size of block, or remove it we occupy the whole thing
    pg->freeSize -= sz;
    if (curBlock->size == sz)
    {
        // Ideal scenario! Just remove this block from the list
        // and return base address
        if (curBlock->prev)
            curBlock->prev->next = curBlock->next;
        if (curBlock->next)
            curBlock->next->prev = curBlock->prev;
        if (curBlock == pg->blockList)
            pg->blockList = curBlock->next;
        curBlock->isFree = false;
        size_t* szEnd = MEM_BLOCK_SIZE_END (curBlock);
        *szEnd = 0;    // Update to 0 so free knows this is in use
        return (void*) (((uintptr_t) curBlock) + MEM_BLOCK_DATA_OFFSET);
    }
    else
    {
        // Block size is greater, we must split this block into two blocks
        // We will lay it out like this: we will use curBlock for the block to
        // return. We will skip over the size, rounded to align to 16 bytes
        uint32_t splitSize = curBlock->size - sz;
        curBlock->size = sz;
        curBlock->isFree = false;
        // Update size at end
        size_t* szEnd = MEM_BLOCK_SIZE_END (curBlock);
        *szEnd = 0;    // Set to 0 so free knows
        void* data = (void*) ((uintptr_t) curBlock) + MEM_BLOCK_DATA_OFFSET;
        memBlock_t* oldBlock = curBlock;
        curBlock = (memBlock_t*) (((uintptr_t) curBlock) + sz);
        curBlock->magic = MEM_BLOCK_MAGIC;
        curBlock->next = oldBlock->next;
        curBlock->prev = oldBlock->prev;
        if (curBlock->next)
            curBlock->next->prev = curBlock;
        if (curBlock->prev)
            curBlock->prev->next = curBlock;
        if (pg->blockList == oldBlock)
            pg->blockList = curBlock;
        curBlock->size = splitSize;
        curBlock->page = pg;
        curBlock->isFree = true;
        // Copy size to end
        size_t* curSzEnd = MEM_BLOCK_SIZE_END (curBlock);
        *curSzEnd = splitSize;
        return data;
    }
}

size_t alignSize (size_t sz)
{
    // Account for block header in size, and then align to 16
    sz += MEM_BLOCK_DATA_OFFSET;
    // Account for size spot at end
    sz += sizeof (size_t);
    if ((sz & (MEM_BLOCK_ALIGN - 1)) != 0)
    {
        sz &= ~(MEM_BLOCK_ALIGN - 1);
        sz += MEM_BLOCK_ALIGN;
    }
    return sz;
}

void* malloc (size_t sz)
{
    if (!sz)
        return NULL;
    sz = alignSize (sz);
    // NOTE: this algorithm is not optimized at all currently
    // It suffers fragmentation and poor performance
    // Find a free block
    memPage_t* curPage = pageList;
    while (curPage)
    {
        if (curPage->magic != MEM_BLOCK_MAGIC)
        {
            // Something is corrupted...
            memCorrupted();
        }
        // Check if have enough space
        if (curPage->freeSize >= sz)
            return allocBlockInPage (curPage, sz);
        curPage = curPage->next;
    }
    // If we get here, there was no page large enough. Allocate a new page
    // Put new pages on the front of the list for performance's sake
    memPage_t* newPage = (memPage_t*) NbFwAllocPage();
    if (!newPage)
    {
        // OOM
        return NULL;
    }
    memPageInit (newPage);
    // Add to list
    newPage->next = pageList;
    newPage->prev = NULL;
    pageList->prev = newPage;
    pageList = newPage;
    // Set up block
    memBlock_t* block = (memBlock_t*) (((uintptr_t) newPage) + MEM_PG_BLOCK_OFFSET);
    memBlockInit (newPage, block);
    newPage->blockList = block;
    // Allocate in this page
    return allocBlockInPage (newPage, sz);
}

static void dumpData()
{
    memPage_t* pg = pageList;
    while (pg)
    {
        NbLogMessageEarly ("Page free size: %u\r\n",
                           NEXBOOT_LOGLEVEL_ERROR,
                           pg->freeSize);
        memBlock_t* b = pg->blockList;
        while (b)
        {
            NbLogMessageEarly ("Block size: %u\r\n",
                               NEXBOOT_LOGLEVEL_ERROR,
                               b->size);
            NbLogMessageEarly ("Is free: %d\r\n", NEXBOOT_LOGLEVEL_ERROR, b->isFree);
            NbLogMessageEarly ("\r\n", NEXBOOT_LOGLEVEL_ERROR);
            b = b->next;
        }
        pg = pg->next;
    }
}

void free (void* ptr)
{
    if (!ptr)
        return;
    // Get block header from ptr
    ptr -= MEM_BLOCK_DATA_OFFSET;
    memBlock_t* block = ptr;
    if (block->magic != MEM_BLOCK_MAGIC || block->page == NULL)
        memCorrupted();
    block->page->freeSize += block->size;
    // Determine if last block is free. If so, merge us
    // First check if there is a last block
    bool merged = false;
    // Determine if next block is free
    memBlock_t* nextBlock = (memBlock_t*) ((void*) block + block->size);
    // Check if a next block exists. If nextBlock is page aligned, that means we are
    // at the end
    if (((uintptr_t) nextBlock & (NEXBOOT_CPU_PAGE_SIZE - 1)) != 0)
    {
        if (nextBlock->magic != MEM_BLOCK_MAGIC || block->page == NULL)
            memCorrupted();
        // Check if block is free
        if (nextBlock->isFree)
        {
            // dumpData();
            //  Merge right, start by replacing current block with a new one
            block->isFree = true;
            block->size += nextBlock->size;
            block->next = nextBlock->next;
            block->prev = nextBlock->prev;
            if (nextBlock->next)
                block->next->prev = block;
            if (nextBlock->prev)
                block->prev->next = block;
            if (block->page->blockList == nextBlock)
                block->page->blockList = block;
            nextBlock->magic = 0;
            // Re write the size
            size_t* szEnd = MEM_BLOCK_SIZE_END (block);
            *szEnd = block->size;
            merged = true;
        }
    }

    if (ptr != ((void*) block->page) + MEM_PG_BLOCK_OFFSET)
    {
        size_t* sz = (void*) (((uintptr_t) block) - sizeof (size_t));
        if (*sz)
        {
            // Block is free. Absorb into previous block
            memBlock_t* prevBlock = (memBlock_t*) (ptr - *sz);
            if (prevBlock->magic != MEM_BLOCK_MAGIC || block->page == NULL)
                memCorrupted();
            prevBlock->size += block->size;
            // Re-write size
            sz = MEM_BLOCK_SIZE_END (prevBlock);
            *sz = prevBlock->size;
            // Update magic number in original block to avoid confusion
            block->magic = 0;
            if (block->isFree)
            {
                // This means that a block was merged in case 1, and needs case 2 as
                // well. To handle that, remove block from list
                if (block->prev)
                    block->prev->next = block->next;
                if (block->next)
                    block->next->prev = block->prev;
            }
            merged = true;
        }
    }
    // If we still haven't merged, then there are no adjacent free blocks. Just
    // add this one to the list and mark it free
    if (!merged)
    {
        block->isFree = true;
        block->next = block->page->blockList;
        block->prev = NULL;
        block->page->blockList->prev = block;
        block->page->blockList = block;
        // Re-write size
        size_t* szEnd = MEM_BLOCK_SIZE_END (block);
        *szEnd = block->size;
    }
    dumpData();
}

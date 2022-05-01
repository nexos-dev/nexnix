/*
    imageList.c - contains functions to build image list
    Copyright 2022 The NexNix Project

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

/// @file imageList.c

#include <conf.h>
#include <libnex.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

// List of created images
static ListHead_t* images = NULL;

// Current line number for diagnostics
static int lineNo = 0;

ListHead_t* createImageList (ListHead_t* confBlocks)
{
    // Create list
    images = ListCreate ("Image_t");
    // Iterate through the configuration blocks
    ListEntry_t* confEntry = ListFront (confBlocks);
    while (confEntry)
    {
        ConfBlock_t* block = ListEntryData (confEntry);
        lineNo = block->lineNo;
        // Figure out what this block is
        if (!strcmp (block->blockType, "image"))
        {
            // Ensure a block name was given
            if (block->blockName[0] == 0)
            {
                error ("%s:%d: image declaration requires name", ConfGetFileName(), lineNo);
                return 0;
            }
        }
        else if (!strcmp (block->blockType, "partition"))
        {
            // Make sure a name wasn't given
            if (block->blockName[0] != 0)
            {
                error ("%s:%d: partition declaration doesn't accept a name", ConfGetFileName(), lineNo);
                return 0;
            }
        }
        else
        {
            // Invalid block
            error ("%s:%d: invalid block type specified", ConfGetFileName(), lineNo);
            return 0;
        }
        confEntry = ListIterate (confEntry);
    }
    return images;
}

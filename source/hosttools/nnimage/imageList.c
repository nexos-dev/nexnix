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

#include "nnimage.h"
#include <conf.h>
#include <libnex.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

// List of created images
static ListHead_t* images = NULL;

// The current partition that is being operated on
static Partition_t* curPart = NULL;

// Current line number for diagnostics
static int lineNo = 0;

// What we are expecting. 1 = image, 2 = partition
static int expecting = 0;

#define EXPECTING_IMAGE     1
#define EXPECTING_PARTITION 2

// The current property name
static const char* prop = NULL;

// If partition was linked to image
static bool wasPartLinked = false;

// Data type union
union val
{
    int64_t numVal;
    char strVal[BLOCK_BUFSZ * 4];
};

ListHead_t* getImages()
{
    return images;
}

// Function to destroy an image
void imageDestroy (void* data)
{
    free (((Image_t*) data)->file);
    ListDestroy (((Image_t*) data)->partsList);
    free (data);
}

// Destroys a partition
void partDestroy (void* data)
{
    free (((Partition_t*) data)->prefix);
    free (data);
}

// Predicate for finding an image
bool imageFindByPredicate (ListEntry_t* entry, void* data)
{
    // Data is image name
    char* s = data;
    if (!strcmp (s, ((Image_t*) ListEntryData (entry))->name))
        return true;
    return false;
}

// Predicate to find a partition
bool partitionFindByPredicate (ListEntry_t* entry, void* data)
{
    char* s = data;
    if (!strcmp (s, ((Partition_t*) ListEntryData (entry))->name))
        return true;
    return false;
}

// Creates an image object
static bool addImage (const char* name)
{
    // Allocate it
    Image_t* img = (Image_t*) calloc_s (sizeof (Image_t));
    if (!img)
        return false;
    img->name = name;
    img->partsList = ListCreate ("Partition_t", false, 0);
    ListSetFindBy (img->partsList, partitionFindByPredicate);
    ListSetDestroy (img->partsList, partDestroy);
    // Add it to the list
    ListAddFront (images, img, 0);
    return true;
}

// Creates a partition object
static bool addPartition (const char* name)
{
    curPart = (Partition_t*) calloc_s (sizeof (Partition_t));
    if (!curPart)
        return false;
    curPart->name = name;
    return true;
}

// Adds a property
bool addProperty (const char* newProp, union val* val, bool isStart, int dataType)
{
    if (isStart)
    {
        prop = newProp;
        return true;
    }
    if (expecting == EXPECTING_IMAGE)
    {
        Image_t* img = ListEntryData (ListFront (images));
        // Decide what property this is
        if (!strcmp (prop, "defaultFile"))
        {
            if (dataType != DATATYPE_STRING)
            {
                error ("%s:%d: property \"defaultFile\" requires a string value", ConfGetFileName(), lineNo);
                return false;
            }
            img->file = (char*) malloc_s (strlen (val->strVal) + 1);
            strcpy (img->file, val->strVal);
        }
        else if (!strcmp (prop, "sizeMul"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"sizeMul\" requires an identifier value", ConfGetFileName(), lineNo);
                return false;
            }
            // Check the size of the multiplier
            if (!strcmp (val->strVal, "KiB"))
                img->mul = IMG_MUL_KIB;
            else if (!strcmp (val->strVal, "MiB"))
                img->mul = IMG_MUL_MIB;
            else if (!strcmp (val->strVal, "GiB"))
                img->mul = IMG_MUL_GIB;
            else
            {
                error ("%s:%d: size multiplier \"%s\" is unsupported", ConfGetFileName(), lineNo, val->strVal);
                return false;
            }
        }
        else if (!strcmp (prop, "size"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error ("%s:%d: property \"size\" requires a numeric value", ConfGetFileName(), lineNo);
                return false;
            }
            img->sz = val->numVal;
        }
        else if (!strcmp (prop, "format"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"format\" requires an identifier value", ConfGetFileName(), lineNo);
                return false;
            }
            if (!strcmp (val->strVal, "gpt"))
                img->format = IMG_FORMAT_GPT;
            else if (!strcmp (val->strVal, "mbr"))
                img->format = IMG_FORMAT_MBR;
            else if (!strcmp (val->strVal, "iso9660"))
                img->format = IMG_FORMAT_ISO9660;
            else if (!strcmp (val->strVal, "floppy"))
                img->format = IMG_FORMAT_FLOPPY;
            else
            {
                error ("%s:%d: image format \"%s\" is unsupported", ConfGetFileName(), lineNo, val->strVal);
                return false;
            }
        }
        else if (!strcmp (prop, "bootMode"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"bootMode\" requires an identifier value", ConfGetFileName(), lineNo);
                return false;
            }
            if (!strcmp (val->strVal, "bios"))
                img->bootMode = IMG_BOOTMODE_BIOS;
            else if (!strcmp (val->strVal, "efi"))
                img->bootMode = IMG_BOOTMODE_EFI;
            else if (!strcmp (val->strVal, "hybrid"))
                img->bootMode = IMG_BOOTMODE_HYBRID;
            else if (!strcmp (val->strVal, "isofloppy"))
                img->bootMode = IMG_BOOTMODE_ISOFLOPPY;
            else if (!strcmp (val->strVal, "default"))
                img->bootMode = IMG_BOOTMODE_DEFAULT;
        }
        else if (!strcmp (prop, "bootEmu"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"bootEmu\" requires and identifier value", ConfGetFileName());
                return false;
            }
            if (!strcmp (val->strVal, "hdd"))
                img->bootEmu = IMG_BOOTEMU_HDD;
            else if (!strcmp (val->strVal, "fdd"))
                img->bootEmu = IMG_BOOTEMU_FDD;
            else if (!strcmp (val->strVal, "noemu"))
                img->bootEmu = IMG_BOOTEMU_NONE;
        }
        else
        {
            error ("%s:%d: property \"%s\" is unsupported", ConfGetFileName(), lineNo, prop);
            return false;
        }
    }
    else if (expecting == EXPECTING_PARTITION)
    {
        if (!strcmp (prop, "start"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error ("%s:%d: property \"start\" requires a numeric value", ConfGetFileName(), lineNo);
                return false;
            }
            curPart->start = val->numVal;
        }
        else if (!strcmp (prop, "size"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error ("%s:%d: property \"size\" requires a numeric value", ConfGetFileName(), lineNo);
                return false;
            }
            curPart->sz = val->numVal;
        }
        else if (!strcmp (prop, "format"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"format\" requires an identifier value", ConfGetFileName(), lineNo);
                return false;
            }
            // Check format specified
            if (!strcmp (val->strVal, "fat32"))
                curPart->filesys = IMG_FILESYS_FAT32;
            else if (!strcmp (val->strVal, "fat16"))
                curPart->filesys = IMG_FILESYS_FAT16;
            else if (!strcmp (val->strVal, "fat12"))
                curPart->filesys = IMG_FILESYS_FAT12;
            else if (!strcmp (val->strVal, "ext2"))
                curPart->filesys = IMG_FILESYS_EXT2;
            else if (!strcmp (val->strVal, "iso9660"))
                curPart->filesys = IMG_FILESYS_ISO9660;
            else
            {
                error ("%s:%d: filesystem \"%s\" is unsupported", ConfGetFileName(), lineNo, val->strVal);
                return false;
            }
        }
        else if (!strcmp (prop, "boot"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"boot\" requires an identifier value", ConfGetFileName(), lineNo);
                return false;
            }
            // Check property value
            if (!strcmp (val->strVal, "true"))
                curPart->isBootPart = true;
            else if (!strcmp (val->strVal, "false"))
                curPart->isBootPart = false;
            else
            {
                error ("%s:%d: property \"boot\" requires a boolean value", ConfGetFileName(), lineNo);
                return false;
            }
        }
        else if (!strcmp (prop, "prefix"))
        {
            if (dataType != DATATYPE_STRING)
            {
                error ("%s:%d: property \"prefix\" requires a string value", ConfGetFileName(), lineNo);
                return false;
            }
            curPart->prefix = malloc_s (strlen (val->strVal) + 1);
            strcpy (curPart->prefix, val->strVal);
        }
        else if (!strcmp (prop, "image"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"image\" requires an identifier value", ConfGetFileName(), lineNo);
                return false;
            }
            // Find the specified image
            ListEntry_t* imgEntry = ListFindEntryBy (images, val->strVal);
            if (!imgEntry)
            {
                error ("%s:%d: image \"%s\" not found", ConfGetFileName(), lineNo, val->strVal);
                return false;
            }
            // Add partititon to image
            ListAddBack (((Image_t*) ListEntryData (imgEntry))->partsList, curPart, 0);
            ((Image_t*) ListEntryData (imgEntry))->partCount++;
            wasPartLinked = true;
        }
        else
        {
            error ("%s:%d: property \"%s\" is unsupported", ConfGetFileName(), lineNo, prop);
            return false;
        }
    }
    return true;
}

ListHead_t* createImageList (ListHead_t* confBlocks)
{
    // Create list
    images = ListCreate ("Image_t", false, 0);
    if (!images)
        return NULL;
    ListSetFindBy (images, imageFindByPredicate);
    ListSetDestroy (images, imageDestroy);
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
                return NULL;
            }
            // Add the image
            if (!addImage (block->blockName))
                return NULL;
            expecting = EXPECTING_IMAGE;
        }
        else if (!strcmp (block->blockType, "partition"))
        {
            // Ensure a block name was given
            if (block->blockName[0] == 0)
            {
                error ("%s:%d: partition declaration requires name", ConfGetFileName(), lineNo);
                return NULL;
            }
            if (!addPartition (block->blockName))
                return NULL;
            expecting = EXPECTING_PARTITION;
        }
        else
        {
            // Invalid block
            error ("%s:%d: invalid block type specified", ConfGetFileName(), lineNo);
            return NULL;
        }
        ListEntry_t* propsEntry = ListFront (block->props);
        while (propsEntry)
        {
            ConfProperty_t* curProp = ListEntryData (propsEntry);
            lineNo = curProp->lineNo;
            // Start new property
            addProperty (curProp->name, NULL, true, 0);
            for (int i = 0; i < curProp->nextVal; ++i)
            {
                lineNo = curProp->lineNo;
                // Declare value union
                union val val;
                if (curProp->vals[i].type == DATATYPE_IDENTIFIER)
                    strcpy (val.strVal, curProp->vals[i].id);
                else if (curProp->vals[i].type == DATATYPE_STRING)
                {
                    mbstate_t mbState = {0};
                    c32stombs (val.strVal, curProp->vals[i].str, (size_t) BLOCK_BUFSZ * 4, &mbState);
                }
                else
                    val.numVal = curProp->vals[i].numVal;
                if (!addProperty (NULL, &val, false, curProp->vals[i].type))
                    return NULL;
            }
            propsEntry = ListIterate (propsEntry);
        }
        if (expecting == EXPECTING_PARTITION)
        {
            // Make sure partition was linked to an image
            if (!wasPartLinked)
            {
                error ("%s:%d: partition \"%s\" not linked to image", ConfGetFileName(), lineNo, curPart->name);
                return NULL;
            }
            curPart = NULL;
        }
        confEntry = ListIterate (confEntry);
    }
    return images;
}

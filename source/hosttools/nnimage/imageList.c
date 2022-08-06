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
#include <libconf/libconf.h>
#include <libnex.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

// List of created images
static ListHead_t* images = NULL;

// The current partition that is being operated on
static Partition_t* curPart = NULL;

// Boot partition
static Partition_t* bootPart = NULL;

// Alternate boot partition
static Partition_t* altBootPart = NULL;

// Current line number for diagnostics
static int lineNo = 0;

// What we are expecting. 1 = image, 2 = partition
static int expecting = 0;

#define EXPECTING_IMAGE     1
#define EXPECTING_PARTITION 2

// The current property name
static const char32_t* prop = NULL;

// If partition was linked to image
static bool wasPartLinked = false;

// Data type union
union val
{
    int64_t numVal;
    char strVal[256];
};

ListHead_t* getImages()
{
    return images;
}

Partition_t* getBootPart()
{
    return bootPart;
}

Partition_t* getAltBootPart()
{
    return altBootPart;
}

// Function to destroy an image
void imageDestroy (const void* data)
{
    free (((Image_t*) data)->file);
    free (((Image_t*) data)->mbrFile);
    ListDestroy (((Image_t*) data)->partsList);
    free ((void*) data);
}

// Destroys a partition
void partDestroy (const void* data)
{
    free (((Partition_t*) data)->prefix);
    free (((Partition_t*) data)->vbrFile);
    free ((void*) data);
}

// Predicate for finding an image
bool imageFindByPredicate (const ListEntry_t* entry, const void* data)
{
    // Data is image name
    const char* s = data;
    if (!strcmp (s, ((Image_t*) ListEntryData (entry))->name))
        return true;
    return false;
}

// Predicate to find a partition
bool partitionFindByPredicate (const ListEntry_t* entry, const void* data)
{
    const char* s = data;
    if (!strcmp (s, ((Partition_t*) ListEntryData (entry))->name))
        return true;
    return false;
}

// Creates an image object
static bool addImage (const char32_t* name)
{
    // Allocate it
    Image_t* img = (Image_t*) calloc_s (sizeof (Image_t));
    if (!img)
        return false;
    // Convert name to multibyte
    mbstate_t mbState = {0};
    char* mbName = malloc_s (c32len (name) * sizeof (char32_t));
    c32stombs (mbName, name, c32len (name), &mbState);
    img->name = mbName;
    img->partsList = ListCreate ("Partition_t", false, 0);
    ListSetFindBy (img->partsList, partitionFindByPredicate);
    ListSetDestroy (img->partsList, partDestroy);
    // Add it to the list
    ListAddFront (images, img, 0);
    return true;
}

// Creates a partition object
static bool addPartition (const char32_t* name)
{
    curPart = (Partition_t*) calloc_s (sizeof (Partition_t));
    if (!curPart)
        return false;
    // Convert name to multibyte
    mbstate_t mbState = {0};
    char* mbName = malloc_s (c32len (name) * sizeof (char32_t));
    c32stombs (mbName, name, c32len (name), &mbState);
    curPart->name = mbName;
    return true;
}

// Adds a property
bool addProperty (const char32_t* newProp,
                  union val* val,
                  bool isStart,
                  int dataType)
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
        if (!c32cmp (prop, U"defaultFile"))
        {
            if (dataType != DATATYPE_STRING)
            {
                error ("%s:%d: property \"defaultFile\" requires a string value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            img->file = (char*) malloc_s (strlen (val->strVal) + 1);
            strcpy (img->file, val->strVal);
        }
        else if (!c32cmp (prop, U"sizeMul"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"sizeMul\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
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
                error ("%s:%d: size multiplier \"%s\" is unsupported",
                       ConfGetFileName(),
                       lineNo,
                       val->strVal);
                return false;
            }
        }
        else if (!c32cmp (prop, U"size"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error ("%s:%d: property \"size\" requires a numeric value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            img->sz = val->numVal;
        }
        else if (!c32cmp (prop, U"format"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"format\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
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
                error ("%s:%d: image format \"%s\" is unsupported",
                       ConfGetFileName(),
                       lineNo,
                       val->strVal);
                return false;
            }
        }
        else if (!c32cmp (prop, U"bootMode"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"bootMode\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            if (!strcmp (val->strVal, "bios"))
                img->bootMode = IMG_BOOTMODE_BIOS;
            else if (!strcmp (val->strVal, "efi"))
                img->bootMode = IMG_BOOTMODE_EFI;
            else if (!strcmp (val->strVal, "hybrid"))
                img->bootMode = IMG_BOOTMODE_HYBRID;
            else if (!strcmp (val->strVal, "noboot"))
                img->bootMode = IMG_BOOTMODE_NOBOOT;
        }
        else if (!c32cmp (prop, U"bootEmu"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"bootEmu\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            if (!strcmp (val->strVal, "hdd"))
                img->bootEmu = IMG_BOOTEMU_HDD;
            else if (!strcmp (val->strVal, "fdd"))
                img->bootEmu = IMG_BOOTEMU_FDD;
            else if (!strcmp (val->strVal, "noemu"))
                img->bootEmu = IMG_BOOTEMU_NONE;
        }
        else if (!c32cmp (prop, U"isUniversal"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"isUniversal\" requires a boolean value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            if (!strcmp (val->strVal, "true"))
                img->isUniversal = true;
            else if (!strcmp (val->strVal, "false"))
                img->isUniversal = false;
            else
            {
                error ("%s:%d: property \"isUniversal\" requires a boolean value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
        }
        else if (!c32cmp (prop, U"mbrFile"))
        {
            if (dataType != DATATYPE_STRING)
            {
                error ("%s:%d: property \"mbrFile\" requires a string value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            img->mbrFile = malloc_s (strlen (val->strVal) + 1);
            strcpy (img->mbrFile, val->strVal);
        }
        else
        {
            error ("%s:%d: property \"%s\" is unsupported",
                   ConfGetFileName(),
                   lineNo,
                   prop);
            return false;
        }
    }
    else if (expecting == EXPECTING_PARTITION)
    {
        if (!c32cmp (prop, U"start"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error ("%s:%d: property \"start\" requires a numeric value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            curPart->start = val->numVal;
        }
        else if (!c32cmp (prop, U"size"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error ("%s:%d: property \"size\" requires a numeric value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            curPart->sz = val->numVal;
        }
        else if (!c32cmp (prop, U"format"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"format\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
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
                error ("%s:%d: filesystem \"%s\" is unsupported",
                       ConfGetFileName(),
                       lineNo,
                       val->strVal);
                return false;
            }
        }
        else if (!c32cmp (prop, U"isBoot"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"isBoot\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            // Make sure this isn't already an alternate boot partition
            if (curPart->isAltBootPart)
            {
                error ("%s:%d: properties \"isAltBoot\" and \"isBoot\" are mutually "
                       "exclusive",
                       ConfGetFileName());
                return false;
            }
            // Check property value
            if (!strcmp (val->strVal, "true"))
                curPart->isBootPart = true;
            else if (!strcmp (val->strVal, "false"))
                curPart->isBootPart = false;
            else
            {
                error (
                    "%s:%d: property \"isBoot\" requires a boolean identifier value",
                    ConfGetFileName(),
                    lineNo);
                return false;
            }
            // Set boot partition
            bootPart = curPart;
        }
        else if (!c32cmp (prop, U"isAltBoot"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"isAltBoot\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            if (curPart->isBootPart)
            {
                error ("%s:%d: properties \"isAltBoot\" and \"isBoot\" are mutually "
                       "exclusive",
                       ConfGetFileName());
                return false;
            }
            // Check property value
            if (!strcmp (val->strVal, "true"))
                curPart->isAltBootPart = true;
            else if (!strcmp (val->strVal, "false"))
                curPart->isAltBootPart = false;
            else
            {
                error ("%s:%d: property \"isAltBoot\" requires a boolean identifier "
                       "value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            // Set boot partition
            altBootPart = curPart;
        }
        /*else if (!strcmp (prop, "isRoot"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"isRoot\" requires an identifier value",
        ConfGetFileName(), lineNo); return false;
            }
            // Check property value
            if (!strcmp (val->strVal, "true"))
                curPart->isRootPart = true;
            else if (!strcmp (val->strVal, "false"))
                curPart->isRootPart = false;
            else
            {
                error ("%s:%d: property \"isRoot\" requires a boolean identifier
        value", ConfGetFileName(), lineNo); return false;
            }
        }*/
        else if (!c32cmp (prop, U"prefix"))
        {
            if (dataType != DATATYPE_STRING)
            {
                error ("%s:%d: property \"prefix\" requires a string value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            curPart->prefix = malloc_s (strlen (val->strVal) + 1);
            strcpy (curPart->prefix, val->strVal);
        }
        else if (!c32cmp (prop, U"image"))
        {
            if (dataType != DATATYPE_IDENTIFIER)
            {
                error ("%s:%d: property \"image\" requires an identifier value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            // Find the specified image
            ListEntry_t* imgEntry = ListFindEntryBy (images, val->strVal);
            if (!imgEntry)
            {
                error ("%s:%d: image \"%s\" not found",
                       ConfGetFileName(),
                       lineNo,
                       val->strVal);
                return false;
            }
            // Add partititon to image
            ListAddBack (((Image_t*) ListEntryData (imgEntry))->partsList,
                         curPart,
                         0);
            ((Image_t*) ListEntryData (imgEntry))->partCount++;
            wasPartLinked = true;
        }
        else if (!c32cmp (prop, U"vbrFile"))
        {
            if (dataType != DATATYPE_STRING)
            {
                error ("%s:%d: property \"vbrFile\" requires a string value",
                       ConfGetFileName(),
                       lineNo);
                return false;
            }
            curPart->vbrFile = malloc_s (strlen (val->strVal) + 1);
            strcpy (curPart->vbrFile, val->strVal);
        }
        else
        {
            error ("%s:%d: property \"%s\" is unsupported",
                   ConfGetFileName(),
                   lineNo,
                   prop);
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
        if (!c32cmp (StrRefGet (block->blockType), U"image"))
        {
            // Ensure a block name was given
            if (!block->blockName)
            {
                error ("%s:%d: image declaration requires name",
                       ConfGetFileName(),
                       lineNo);
                return NULL;
            }
            // Add the image
            if (!addImage (StrRefGet (block->blockName)))
                return NULL;
            expecting = EXPECTING_IMAGE;
        }
        else if (!c32cmp (StrRefGet (block->blockType), U"partition"))
        {
            // Ensure a block name was given
            if (!block->blockName)
            {
                error ("%s:%d: partition declaration requires name",
                       ConfGetFileName(),
                       lineNo);
                return NULL;
            }
            if (!addPartition (StrRefGet (block->blockName)))
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
            addProperty (StrRefGet (curProp->name), NULL, true, 0);
            for (int i = 0; i < curProp->nextVal; ++i)
            {
                lineNo = curProp->lineNo;
                // Declare value union
                union val val;
                if (curProp->vals[i].type == DATATYPE_STRING ||
                    curProp->vals[i].type == DATATYPE_IDENTIFIER)
                {
                    mbstate_t mbState = {0};
                    c32stombs (val.strVal,
                               StrRefGet (curProp->vals[i].str),
                               256,
                               &mbState);
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
                error ("%s:%d: partition \"%s\" not linked to image",
                       ConfGetFileName(),
                       lineNo,
                       curPart->name);
                return NULL;
            }
            curPart = NULL;
        }
        confEntry = ListIterate (confEntry);
    }
    return images;
}

/*
    conf.c - contains configuration file parser core
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

/// @file conf.c

#include "include/internal.h"
#include <conf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The name of the file being read
static const char* fileName = NULL;

PUBLIC ListHead_t* ConfInit (const char* file)
{
    fileName = file;
    return _confParse (file);
}

PUBLIC const char* ConfGetFileName (void)
{
    return fileName;
}

PUBLIC void _confSetFileName (const char* file)
{
    fileName = file;
}

PUBLIC void ConfFreeParseTree (ListHead_t* list)
{
    // Destroy each property list
    ListEntry_t* entry = ListFront (list);
    while (entry)
    {
        ListRef (entry);
        ListDestroy ((ListHead_t*) ((ConfBlock_t*) ListEntryData (entry))->props);
        ListEntry_t* oldEntry = entry;
        entry = ListIterate (entry);
        ListDeRef (oldEntry);
    }
    // Destroy the list
    ListDestroy (list);
}

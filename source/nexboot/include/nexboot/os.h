/*
    os.h - contains OS booting structures
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

#ifndef _OS_H
#define _OS_H

#include <libnex/array.h>
#include <libnex/stringref.h>

// OS boot info structure
typedef struct _os
{
    int bootType;            // OS boot type
    StringRef_t* payload;    // OS boot payload
    Array_t* mods;           // OS boot modules
    StringRef_t* args;       // Command line arguments
} NbOsInfo_t;

// Valid boottypes
#define NB_BOOTTYPE_NEXNIX    1
#define NB_BOOTTYPE_CHAINLOAD 2

static const char* nbBootTypes[] = {"", "nexnix", "chainload"};

// Boot type flags
#define NB_BOOTTYPE_SUPPORTS_MODS (1 << 0)
#define NB_BOOTTYPE_SUPPORTS_ARGS (1 << 1)

// Flags table
static uint32_t nbBootTypeFlags[] = {
    0,
    NB_BOOTTYPE_SUPPORTS_ARGS | NB_BOOTTYPE_SUPPORTS_MODS,
    0};

#define NB_BOOT_MODS_MAX     128
#define NB_BOOT_MODS_INITIAL 8

// Boot type functions
typedef bool (*NbOsBoot) (NbOsInfo_t*);

bool NbOsBootNexNix (NbOsInfo_t* os);
bool NbOsBootChainload (NbOsInfo_t* os);

static NbOsBoot nbBootTab[] = {NULL, NbOsBootNexNix, NbOsBootChainload};

#endif

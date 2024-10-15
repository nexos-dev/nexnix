/*
    main.c - contains kernel entry point
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

#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <stdlib.h>
#include <string.h>

// Boot info
static NexNixBoot_t* bootInfo = NULL;
static SlabCache_t* bootInfCache = NULL;
static char* cmdLine = NULL;

// Returns boot arguments
NexNixBoot_t* NkGetBootArgs()
{
    return bootInfo;
}

const char* NkReadArg (const char* arg)
{
    // Move through string, looking for arg
    const char* cmdLineEnd = cmdLine + strlen (cmdLine);
    size_t argLen = strlen (arg);
    const char* iter = cmdLine;
    while (iter < ((cmdLineEnd + 1) - argLen))
    {
        if (!memcmp (arg, iter, argLen))
        {
            iter += argLen;
            // If next character is null terminator, then we are at the end
            if (*iter == 0)
                return "";
            // Ensure next character is a space
            if (*iter != ' ')
                goto next;
            ++iter;
            // Check if next character is dash
            if (*iter == '-')
                return "";    // Argument has no value
            // Get length of argument
            char tmp[128] = {0};
            int i = 0;
            while (*iter != ' ' && *iter)
                tmp[i] = *iter, ++i, ++iter;
            // kmalloc it
            char* buf = kmalloc (i);
            strcpy (buf, tmp);
            return (const char*) buf;
        }
    next:
        ++iter;
    }
    return NULL;
}

// Helper function to compute checksums
bool NkVerifyChecksum (uint8_t* buf, size_t len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; ++i)
        sum += buf[i];
    return !sum;
}

void NkMain (NexNixBoot_t* bootinf)
{
    // Set bootinfo
    bootInfo = bootinf;
    // Initialize MM phase 1
    MmInitPhase1();
    // Copy bootinfo into cache
    bootInfCache = MmCacheCreate (sizeof (NexNixBoot_t), "NexNixBoot_t", 0, 0);
    NexNixBoot_t* bootInf = MmCacheAlloc (bootInfCache);
    memcpy (bootInf, bootInfo, sizeof (NexNixBoot_t));
    bootInfo = bootInf;
    // Move boot arguments into better spot
    size_t argLen = strlen (bootInfo->args);
    cmdLine = kmalloc (argLen + 1);
    strcpy (cmdLine, bootInfo->args);
    // Initialize boot drivers
    PltInitDrvs();
    // Initialize log
    NkLogInit();
    // Print banner
    NkLogInfo ("\
NexNix version %s\n\
Copyright (C) 2023 - 2024 The Nexware Project\n",
               NEXNIX_VERSION);
    // Initialize resource manager
    NkInitResource();
    // Initialize CCB
    CpuInitCcb();
    // Initialize phase 2 of platform
    PltInitPhase2();
    // Initialize MM phase 2
    MmInitPhase2();
    // Initialize phase 3 of platform
    PltInitPhase3();
    // Initialize timing subsystem
    NkInitTime();
    MmSlabDump();
    for (;;)
        ;
}

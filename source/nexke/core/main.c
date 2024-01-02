/*
    main.c - contains kernel entry point
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

#include <nexke/cpu.h>
#include <nexke/nexboot.h>

// Boot info
static NexNixBoot_t* bootInfo = NULL;

// Returns boot arguments
NexNixBoot_t* NkGetBootArgs()
{
    return bootInfo;
}

// Boot argument parser
static void nkArgsParse()
{
    // Grab arguments
}

void NkMain (NexNixBoot_t* bootinf)
{
    uint32_t* fb = bootinf->display.frameBuffer;
    fb += (bootinf->display.bytesPerLine * 20);
    for (int i = 0; i < bootinf->display.width; ++i)
        fb[i] = 0xFFFFFFFF;
    // Set bootinfo
    bootInfo = bootinf;
    // Initialize first CCB
    CpuInitCcb();
    for (;;)
        ;
}

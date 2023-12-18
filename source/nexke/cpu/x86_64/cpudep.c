/*
    cpudep.c - contains CPU dependent part of nexke
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
#include <string.h>

// Globals

// The system's CCB. A very important data structure that contains the kernel's
// deepest bowels
static NkCcb_t ccb = {0};    // The CCB

// Prepares CCB data structure. This is the first thing called during boot
void CpuInitCcb()
{
    // Grab boot info
    NexNixBoot_t* bootInfo = NkGetBootArgs();
    // Set up basic fields
    ccb.cpuArch = NEXKE_CPU_X86_64;
    ccb.cpuFamily = NEXKE_CPU_FAMILY_X86;
#ifdef NEXNIX_BOARD_PC
    ccb.sysBoard = NEXKE_BOARD_PC;
#else
#error Unrecognized board
#endif
    strcpy (ccb.sysName, bootInfo->sysName);
}

// Returns CCB to caller
NkCcb_t* CpuGetCcb()
{
    return &ccb;
}

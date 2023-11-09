/*
    bios.c - contains BIOS abstraction layer of nexboot
    Copyright 2022, 2023 The NexNix Project

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
#include <string.h>

// Location of bioscall blob
#define NEXBOOT_BIOSCALL_BLOB 0x1000

// Calls NbBiosCall in binary blob
void NbBiosCall (uint32_t intNo, NbBiosRegs_t* in, NbBiosRegs_t* out)
{
    // Create function pointer for bioscall
    void (*bioscall) (uint32_t, NbBiosRegs_t*, NbBiosRegs_t*) =
        (void*) NEXBOOT_BIOSCALL_BLOB;
    bioscall (intNo, in, out);
}

// Print a character to the BIOS screen
void NbFwEarlyPrint (char c)
{
    NbBiosRegs_t in = {0}, out = {0};
    in.ah = 0x0E;
    in.al = (uint8_t) c;
    NbBiosCall (0x10, &in, &out);
}

uintptr_t curMemLocation = NEXBOOT_BIOS_MEMBASE;

uintptr_t NbFwAllocPage()
{
    uintptr_t ret = curMemLocation;
    curMemLocation += NEXBOOT_CPU_PAGE_SIZE;
    if (curMemLocation >= NEXBOOT_BIOS_BASE)
    {
        curMemLocation -= NEXBOOT_CPU_PAGE_SIZE;
        return 0;
    }
    memset ((void*) ret, 0, NEXBOOT_CPU_PAGE_SIZE);
    return ret;
}

uintptr_t NbFwAllocPages (int count)
{
    uintptr_t ret = curMemLocation;
    curMemLocation += (NEXBOOT_CPU_PAGE_SIZE * count);
    if (curMemLocation >= NEXBOOT_BIOS_BASE)
    {
        curMemLocation -= (NEXBOOT_CPU_PAGE_SIZE * count);
        return 0;
    }
    memset ((void*) ret, 0, (NEXBOOT_CPU_PAGE_SIZE * count));
    return ret;
}

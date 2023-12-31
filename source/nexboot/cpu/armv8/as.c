/*
    as.c - contains address space management code
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

#include <assert.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <string.h>

// This module is designed to be ultra simple. It makes a few assumptions,
// the big one being that all page tables come from identity mapped address space
// This makes the code fairly simple, as accessing the page tables is easy.
// Also, it is designed to be very temporary and rudimentary, as we have very basic
// requirements

// Initializes address space manager
void NbCpuAsInit()
{
}

// Maps address into space
bool NbCpuAsMap (uintptr_t virt, paddr_t phys, uint32_t flags)
{
    return true;
}

// Unmaps address from address space
void NbCpuAsUnmap (uintptr_t virt)
{
}

// Enables paging
void NbCpuEnablePaging()
{
}

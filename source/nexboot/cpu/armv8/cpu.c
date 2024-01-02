/*
    cpu.c - contains CPU specific abstractions
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

#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <string.h>

void NbCpuLaunchKernelAsm (uintptr_t entry, uintptr_t bootInfo, uintptr_t stack);

void NbCpuLaunchKernel (uintptr_t entry, uintptr_t bootInfo)
{
    NbCpuLaunchKernelAsm (entry, bootInfo, NB_KE_STACK_BASE - 16);
}

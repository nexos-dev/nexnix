/*
    cpu.c - contains CPU specific abstractions
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

#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/nexnix.h>
#include <string.h>

void NbCpuLaunchKernelAsm (uintptr_t entry, uintptr_t bootInf);

void NbCpuLaunchKernel (uintptr_t entry, uintptr_t bootInf)
{
    // Configure PMP
    uint64_t pmpcfg = 0xF;
    uint64_t pmpaddr = 0xFFFFFFFFFFFFFFFF;
    NbCpuWriteCsr ("pmpcfg0", pmpcfg);
    NbCpuWriteCsr ("pmpaddr0", pmpaddr);
    // Check for supervisor mode
    if (!(NbCpuReadCsr ("misa") & (1 << 20)))
    {
        NbLogMessage ("nexboot: error: Supervisor mode required\n", NEXBOOT_LOGLEVEL_EMERGENCY);
        NbCrash();
    }
    // Fill out CPU fields
    NexNixBoot_t* nnBoot = (NexNixBoot_t*) bootInf;
    nnBoot->cpu.misa = NbCpuReadCsr ("misa");
    nnBoot->cpu.mimpid = NbCpuReadCsr ("mimpid");
    nnBoot->cpu.marchid = NbCpuReadCsr ("marchid");
    nnBoot->cpu.mvendorid = NbCpuReadCsr ("mvendorid");
    // Set satp
    NbCpuWriteCsr ("satp", NbCpuGetSatp());
    // Set up mstatus
    uint64_t mstatus = NbCpuReadCsr ("mstatus");
    mstatus |= (1 << 11);    // Set MPP to S-mode
    mstatus &= ~(1 << 5);    // Clear SPIE
    NbCpuWriteCsr ("mstatus", mstatus);
    // Delegate all possible interrupts to S-mode
    NbCpuWriteCsr ("mideleg", 0xFFFFFFFFFFFFFFFF);
    NbCpuWriteCsr ("medeleg", 0xFFFFFFFFFFFFFFFF);
    // Finish the job
    NbCpuLaunchKernelAsm (entry, bootInf);
}

/*
    exec.c - contains exception handlers
    Copyright 2024 The NexNix Project

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
#include <nexke/cpu.h>
#include <nexke/mm.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <string.h>

// Exception name table
static const char* cpuExecNameTab[] = {"division by zero",
                                       "debug failure",
                                       "NMI",
                                       "bad breakpoint",
                                       "overflow",
                                       "bound range exceeded",
                                       "invalid opcode",
                                       "FPU not available",
                                       "double fault",
                                       "coprocessor overrun",
                                       "invalid TSS",
                                       "segment not present",
                                       "stack fault",
                                       "general protection fault",
                                       "page fault",
                                       "FPU error",
                                       "aligment check failure",
                                       "machine check",
                                       "SIMD failure",
                                       "virtualization failure",
                                       "control flow failure"};

// System page fault handler
static bool CpuPageFault (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    return false;
}

// Gets exception diagnostic info
void CpuGetExecInf (CpuExecInf_t* out, NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Ensure exception is within bounds
    if (intObj->vector > CPU_EXEC_MAX)
        PltBadTrap (ctx, "invalid exception");    // Very odd indeed
    out->name = cpuExecNameTab[intObj->vector];
}

// Registers exception handlers
void CpuRegisterExecs()
{
    // Install all interrupts
    PltInstallInterrupt (CPU_EXEC_PF, PLT_INT_EXEC, PLT_NO_IPL, CpuPageFault);
}

/*
    cpuhelp.c - contains CPU helper functions
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

#include <nexke/cpu.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <stdarg.h>
#include <stdio.h>

void CpuIoWait()
{
    CpuOutb (0x80, 0);
}

void CpuOutb (uint16_t port, uint8_t val)
{
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void CpuOutw (uint16_t port, uint16_t val)
{
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void CpuOutl (uint16_t port, uint32_t val)
{
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t CpuInb (uint16_t port)
{
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint16_t CpuInw (uint16_t port)
{
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t CpuInl (uint16_t port)
{
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t CpuReadCr0()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr0, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr0 (uint32_t val)
{
    asm volatile ("mov %0, %%cr0" : : "a"(val));
}

uint32_t CpuReadCr3()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr3, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr3 (uint32_t val)
{
    asm volatile ("mov %0, %%cr3" : : "a"(val));
}

uint32_t CpuReadCr4()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr4, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr4 (uint32_t val)
{
    asm volatile ("mov %0, %%cr4" : : "a"(val));
}

uint32_t CpuReadCr2()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr2, %0" : "=a"(ret));
    return ret;
}

void CpuWrmsr (uint32_t msr, uint64_t val)
{
    asm volatile ("wrmsr" : : "c"(msr), "a"((uint32_t) val), "d"((uint32_t) (val >> 32ULL)));
}

uint64_t CpuRdmsr (uint32_t msr)
{
    uint32_t ax, dx;
    asm volatile ("rdmsr" : "=a"(ax), "=d"(dx) : "c"(msr));
    return ax | ((uint64_t) dx << 32);
}

uint64_t CpuRdtsc (void)
{
    uint32_t low, high;
    asm volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t) high << 32) | low;
}

void CpuDisable()
{
    asm ("cli");
}

void CpuEnable()
{
    if (!CpuGetCcb()->archCcb.intsHeld)
        asm ("sti");
    else
        CpuGetCcb()->archCcb.intRequested = true;
}

void CpuHoldInts()
{
    CpuGetCcb()->archCcb.intsHeld = true;
}

void CpuUnholdInts()
{
    CpuGetCcb()->archCcb.intsHeld = false;
    if (CpuGetCcb()->archCcb.intRequested)
    {
        asm ("sti");
        CpuGetCcb()->archCcb.intRequested = false;
    }
}

void __attribute__ ((noreturn)) CpuCrash()
{
    asm ("cli;hlt");
    for (;;)
        ;
}

void CpuPrintDebug (CpuIntContext_t* context)
{
    // Basically we just dump all the registers
    va_list ap;
    NkLogMessage ("CPU dump:\n", NK_LOGLEVEL_EMERGENCY, ap);
    char buf[2048] = {0};
    sprintf (buf,
             "eax: %#08lX ebx: %#08lX ecx: %#08X edx: %#08X\n",
             context->eax,
             context->ebx,
             context->ecx,
             context->edx);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "esi: %#08lX edi: %#08lX ebp: %#08lX esp: %#08lX\n",
             context->esi,
             context->edi,
             context->ebp,
             context->esp);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    if (CpuGetCcb()->cpuFamily > 4)
    {
        sprintf (buf,
                 "cr0: %#08lX cr2: %#08lX cr3: %#08lX cr4: %#08lX\n",
                 CpuReadCr0(),
                 CpuReadCr2(),
                 CpuReadCr3(),
                 CpuReadCr4());
    }
    else
    {
        sprintf (buf,
                 "cr0: %#08lX cr2: %#08lX cr3: %#08lX\n",
                 CpuReadCr0(),
                 CpuReadCr2(),
                 CpuReadCr3());
    }
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "cs: %#02lX ds: %#02lX es: %#02lX ss: %#02lX\n",
             context->cs,
             context->ds,
             context->es,
             context->ss);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "eip: %#08lx eflags: %#08lx errcode: %#lX intno: %#02lX",
             context->eip,
             context->eflags,
             context->errCode,
             context->intNo);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
}

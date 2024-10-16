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

uint64_t CpuReadCr0()
{
    uint64_t ret = 0;
    asm volatile ("mov %%cr0, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr0 (uint64_t val)
{
    asm volatile ("mov %0, %%cr0" : : "a"(val));
}

uint64_t CpuReadCr3()
{
    uint64_t ret = 0;
    asm volatile ("mov %%cr3, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr3 (uint64_t val)
{
    asm volatile ("mov %0, %%cr3" : : "a"(val));
}

uint64_t CpuReadCr4()
{
    uint64_t ret = 0;
    asm volatile ("mov %%cr4, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr4 (uint64_t val)
{
    asm volatile ("mov %0, %%cr4" : : "a"(val));
}

uint64_t CpuReadCr2()
{
    uint64_t ret = 0;
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
    return ax | ((uint64_t) dx << 32ULL);
}

uint64_t CpuRdtsc (void)
{
    uint32_t low, high;
    asm volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t) high << 32) | low;
}

void CpuInvlpg (uintptr_t addr)
{
    asm volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

void CpuHalt()
{
    asm volatile ("hlt");
}

void CpuSetGs (uintptr_t addr)
{
    CpuWrmsr (0xC0000101, addr);
}

void __attribute__ ((noreturn)) CpuCrash()
{
    asm ("cli;hlt");
    for (;;)
        ;
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

void CpuPrintDebug (CpuIntContext_t* context)
{
    // Basically we just dump all the registers
    va_list ap;
    NkLogMessage ("CPU dump:\n", NK_LOGLEVEL_EMERGENCY, ap);
    char buf[2048] = {0};
    sprintf (buf,
             "rax: %#016llX rbx: %#016llX rcx: %#08llX rdx: %#016llX\n",
             context->rax,
             context->rbx,
             context->rcx,
             context->rdx);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "rsi: %#016llX rdi: %#016llX rbp: %#016llX rsp: %#016llX\n",
             context->rsi,
             context->rdi,
             context->rbp,
             context->rsp);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "r8: %#016llX r9: %#016llX r10: %#016llX r11: %#016llX\n",
             context->r8,
             context->r9,
             context->r10,
             context->r11);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "r12: %#016llX r13: %#016llX r14: %#016llX r15: %#016llX\n",
             context->r8,
             context->r9,
             context->r10,
             context->r11);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "cr0: %#016llX cr2: %#016llX cr3: %#016llX cr4: %#016llX\n",
             CpuReadCr0(),
             CpuReadCr2(),
             CpuReadCr3(),
             CpuReadCr4());
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "rip: %#016llx rflags: %#016llx errcode: %#lX intno: %#02lX",
             context->rip,
             context->rflags,
             context->errCode,
             context->intNo);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
}

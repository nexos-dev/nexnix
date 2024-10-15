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
#include <string.h>

extern bool printEarlyDisabled;

// Prints a stack trace
static void nbTraceStack()
{
    // Get EBP
    uint32_t curFrame = 0;
    asm volatile ("mov %%ebp, %0" : "=r"(curFrame));
    if (printEarlyDisabled)
        NbLogMessage ("\nStack trace:\n", NEXBOOT_LOGLEVEL_DEBUG);
    else
        NbLogMessageEarly ("\nStack trace:\n", NEXBOOT_LOGLEVEL_DEBUG);
    while (curFrame)
    {
        // Print out frame info
        uint32_t* stackFrame = (uint32_t*) curFrame;
        if (printEarlyDisabled)
            NbLogMessage ("%#X: %#X\n", NEXBOOT_LOGLEVEL_DEBUG, *stackFrame, *(stackFrame + 1));
        else
            NbLogMessageEarly ("%#X: %#X\r\n",
                               NEXBOOT_LOGLEVEL_DEBUG,
                               *stackFrame,
                               *(stackFrame + 1));
        curFrame = *stackFrame;
    }
}

void NbCrash()
{
    nbTraceStack();
    //  Halt system
    asm ("cli; hlt");
}

void NbIoWait()
{
    asm ("mov $0, %al; outb %al, $0x80");
}

void NbOutb (uint16_t port, uint8_t val)
{
    NbIoWait();
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void NbOutw (uint16_t port, uint16_t val)
{
    NbIoWait();
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void NbOutl (uint16_t port, uint32_t val)
{
    NbIoWait();
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t NbInb (uint16_t port)
{
    NbIoWait();
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint16_t NbInw (uint16_t port)
{
    NbIoWait();
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t NbInl (uint16_t port)
{
    NbIoWait();
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t NbReadCr0()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr0, %0" : "=a"(ret));
    return ret;
}

void NbWriteCr0 (uint32_t val)
{
    asm volatile ("mov %0, %%cr0" : : "a"(val));
}

uint32_t NbReadCr3()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr3, %0" : "=a"(ret));
    return ret;
}

void NbWriteCr3 (uint32_t val)
{
    asm volatile ("mov %0, %%cr3" : : "a"(val));
}

uint32_t NbReadCr4()
{
    uint32_t ret = 0;
    asm volatile ("mov %%cr4, %0" : "=a"(ret));
    return ret;
}

void NbWriteCr4 (uint32_t val)
{
    asm volatile ("mov %0, %%cr4" : : "a"(val));
}

void NbWrmsr (uint32_t msr, uint64_t val)
{
    asm volatile ("wrmsr" : : "c"(msr), "a"((uint32_t) val), "d"((uint32_t) (val >> 32ULL)));
}

uint64_t NbRdmsr (uint32_t msr)
{
    uint32_t ax, dx;
    asm volatile ("rdmsr" : "=a"(ax), "=d"(dx) : "c"(msr));
    return ax | ((uint64_t) dx << 32);
}

void NbInvlpg (uintptr_t addr)
{
    asm volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

void nbCpuAsmLaunch (uintptr_t stack, uintptr_t entry, uintptr_t bootInf);

void NbCpuLaunchKernel (uintptr_t entry, uintptr_t bootInf)
{
    // Launch it
    nbCpuAsmLaunch (NB_KE_STACK_BASE - 16, entry, bootInf);
}

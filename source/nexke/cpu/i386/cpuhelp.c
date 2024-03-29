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

void CpuIoWait()
{
    asm("mov $0, %al; outb %al, $0x80");
}

void CpuOutb (uint16_t port, uint8_t val)
{
    CpuIoWait();
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void CpuOutw (uint16_t port, uint16_t val)
{
    CpuIoWait();
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

void CpuOutl (uint16_t port, uint32_t val)
{
    CpuIoWait();
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t CpuInb (uint16_t port)
{
    CpuIoWait();
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint16_t CpuInw (uint16_t port)
{
    CpuIoWait();
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t CpuInl (uint16_t port)
{
    CpuIoWait();
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t CpuReadCr0()
{
    uint32_t ret = 0;
    asm volatile("mov %%cr0, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr0 (uint32_t val)
{
    asm volatile("mov %0, %%cr0" : : "a"(val));
}

uint32_t CpuReadCr3()
{
    uint32_t ret = 0;
    asm volatile("mov %%cr3, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr3 (uint32_t val)
{
    asm volatile("mov %0, %%cr3" : : "a"(val));
}

uint32_t CpuReadCr4()
{
    uint32_t ret = 0;
    asm volatile("mov %%cr4, %0" : "=a"(ret));
    return ret;
}

void CpuWriteCr4 (uint32_t val)
{
    asm volatile("mov %0, %%cr4" : : "a"(val));
}

void CpuWrmsr (uint32_t msr, uint64_t val)
{
    asm volatile("wrmsr" : : "c"(msr), "a"((uint32_t) val), "d"((uint32_t) (val >> 32ULL)));
}

uint64_t CpuRdmsr (uint32_t msr)
{
    uint32_t ax, dx;
    asm volatile("rdmsr" : "=a"(ax), "=d"(dx) : "c"(msr));
    return ax | ((uint64_t) dx << 32);
}

void __attribute__ ((noreturn)) CpuCrash()
{
    asm("cli;hlt");
    for (;;)
        ;
}

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

void __attribute__ ((noreturn)) CpuCrash()
{
    asm("cli;hlt");
    for (;;)
        ;
}

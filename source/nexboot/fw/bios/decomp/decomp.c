/*
    decomp.c - contains linker script for ndecomp
    Copyright 2022, 2023 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    There should be a copy of the License distributed in a file named
    LICENSE, if not, you may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "tinf/tinf.h"
#include <elf.h>
#include <nexboot/detect.h>
#include <stdint.h>
#include <string.h>

// Constants
#define NEXBOOT_BASE_ADDR 0x100000
#define NEXBOOT_MAX_SIZE  0x80000
#define NEXBOOT_MAIN_BASE 0x190000

#define HALT asm("cli; hlt")

void NbDecompMain (NbloadDetect_t* nbDetect, uint8_t* nbBase, uint32_t nbSize)
{
    // Decompress it
    uint32_t size = NEXBOOT_MAX_SIZE;
    int res =
        tinf_gzip_uncompress ((void*) NEXBOOT_BASE_ADDR, &size, nbBase, nbSize);
    if (res)
    {
        // We can't print anything right now, just halt as the chance of this
        // is extremely low
        HALT;
    }
    // Load up ELF to 0x190000
    Elf32_Ehdr* ehdr = (Elf32_Ehdr*) NEXBOOT_BASE_ADDR;
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3)

    {
        HALT;
    }
    Elf32_Phdr* phdr = (Elf32_Phdr*) (NEXBOOT_BASE_ADDR + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        memcpy ((void*) phdr[i].p_vaddr,
                (void*) (phdr[i].p_offset + NEXBOOT_BASE_ADDR),
                phdr[i].p_filesz);
        memset ((void*) (phdr[i].p_vaddr + phdr[i].p_filesz),
                0,
                phdr[i].p_memsz - phdr[i].p_filesz);
    }
    void (*NexBoot)() = (void*) ehdr->e_entry;
    NexBoot();
    // Freeze if we return
    for (;;)
        asm("hlt");
}

// Stub assert
void __attribute__ ((noreturn))
__assert_failed (const char* expr, const char* file, int line, const char* func)
{
    asm("cli;hlt");
    __builtin_unreachable();
}

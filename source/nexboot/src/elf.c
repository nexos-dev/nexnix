/*
    elf.c - contains ELF loader
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
#include <elf.h>
#include <nexboot/fw.h>
#include <nexboot/nexboot.h>
#include <nexboot/os.h>
#include <nexboot/shell.h>
#include <string.h>

static bool elf32CheckArch()
{
#if defined(NEXNIX_ARCH_I386)
    return true;
#else
    return false;
#endif
}

static bool elf64CheckArch()
{
#if defined(NEXNIX_ARCH_X86_64)
    return true;
#else
    return false;
#endif
}

// Check ELF machine
static bool elfCheckMachine (int mach)
{
#if defined(NEXNIX_ARCH_I386)
    if (mach != EM_386)
        return false;
#elif defined(NEXNIX_ARCH_X86_64)
    if (mach != EM_X86_64)
        return false;
#endif
    return true;
}

uintptr_t NbElfLoadFile (void* fileBase)
{
    Elf32_Ehdr* hdr32 = (Elf32_Ehdr*) fileBase;
    // Verify header
    if (hdr32->e_ident[EI_MAG0] != ELFMAG0 || hdr32->e_ident[EI_MAG1] != ELFMAG1 ||
        hdr32->e_ident[EI_MAG2] != ELFMAG2 || hdr32->e_ident[EI_MAG3] != ELFMAG3)
    {
        NbShellWrite ("nexboot: invalid ELF header\n");
        return 0;
    }
    // Check class
    if (hdr32->e_ident[EI_CLASS] == ELFCLASS32)
    {
        // Ensure architecture of system is compatible
        if (!elf32CheckArch())
        {
            NbShellWrite ("nexboot: system architecure incompatible with payload "
                          "architecture\n");
            return 0;
        }
        // Check endianess
        if (hdr32->e_ident[EI_DATA] != ELFDATA2LSB)
        {
            NbShellWrite ("nexboot: little endian payload required\n");
            return 0;
        }
        // Check machine
        if (!elfCheckMachine (hdr32->e_machine))
        {
            NbShellWrite ("nexboot: payload machine type incompatible with system\n");
            return 0;
        }
        // Grab program headers
        uint32_t phSize = hdr32->e_phentsize;
        Elf32_Phdr* phdr = (Elf32_Phdr*) (fileBase + hdr32->e_phoff);
        for (int i = 0; i < hdr32->e_phnum; ++i)
        {
            if (phdr->p_type == PT_LOAD)
            {
                // Determine number of pages in this segment
                uint32_t numPgs =
                    (phdr->p_memsz + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
                // Allocate pages
                void* filePhys = (void*) NbFwAllocPages (numPgs);
                // Copy file data to physical address
                void* srcBuf = fileBase + phdr->p_offset;
                memcpy (filePhys, srcBuf, phdr->p_filesz);
                // Zero out memory and file difference
                memset (filePhys + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
                // Get permissions flags
                uint32_t flags = NB_CPU_AS_GLOBAL | NB_CPU_AS_NX;
                if (phdr->p_flags & PF_X)
                    flags &= ~(NB_CPU_AS_NX);
                if (phdr->p_flags & PF_W)
                    flags |= NB_CPU_AS_RW;
                // Map pages
                for (int i = 0; i < numPgs; ++i)
                {
                    NbCpuAsMap (phdr->p_vaddr + (i * NEXBOOT_CPU_PAGE_SIZE),
                                (paddr_t) filePhys + (i * NEXBOOT_CPU_PAGE_SIZE),
                                flags);
                }
            }
            // To next header
            phdr = (void*) phdr + phSize;
        }
        // Return entry point
        return hdr32->e_entry;
    }
    else if (hdr32->e_ident[EI_CLASS] == ELFCLASS64)
    {
        // Ensure architecture of system is compatible
        if (!elf64CheckArch())
        {
            NbShellWrite ("nexboot: system architecure incompatible with payload "
                          "architecture\n");
            return 0;
        }
        Elf64_Ehdr* hdr64 = (Elf64_Ehdr*) hdr32;
        // Check endianess
        if (hdr64->e_ident[EI_DATA] != ELFDATA2LSB)
        {
            NbShellWrite ("nexboot: little endian payload required\n");
            return 0;
        }
        // Check machine
        if (!elfCheckMachine (hdr64->e_machine))
        {
            NbShellWrite ("nexboot: payload machine type incompatible with system\n");
            return 0;
        }
        // Grab program headers
        uint32_t phSize = hdr64->e_phentsize;
        Elf64_Phdr* phdr = (Elf64_Phdr*) (fileBase + hdr64->e_phoff);
        for (int i = 0; i < hdr64->e_phnum; ++i)
        {
            if (phdr->p_type == PT_LOAD)
            {
                // Determine number of pages in this segment
                uint32_t numPgs =
                    (phdr->p_memsz + (NEXBOOT_CPU_PAGE_SIZE - 1)) / NEXBOOT_CPU_PAGE_SIZE;
                // Allocate pages
                void* filePhys = (void*) NbFwAllocPages (numPgs);
                // Copy file data to physical address
                void* srcBuf = fileBase + phdr->p_offset;
                memcpy (filePhys, srcBuf, phdr->p_filesz);
                // Zero out memory and file difference
                memset (filePhys + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
                // Get permissions flags
                uint32_t flags = NB_CPU_AS_GLOBAL | NB_CPU_AS_NX;
                if (phdr->p_flags & PF_X)
                    flags &= ~(NB_CPU_AS_NX);
                if (phdr->p_flags & PF_W)
                    flags |= NB_CPU_AS_RW;
                // Map pages
                for (int i = 0; i < numPgs; ++i)
                {
                    NbCpuAsMap (phdr->p_vaddr + (i * NEXBOOT_CPU_PAGE_SIZE),
                                (paddr_t) filePhys + (i * NEXBOOT_CPU_PAGE_SIZE),
                                flags);
                }
            }
            // To next header
            phdr = (void*) phdr + phSize;
        }
        // Return entry point
        return hdr64->e_entry;
    }
    assert (0);
}

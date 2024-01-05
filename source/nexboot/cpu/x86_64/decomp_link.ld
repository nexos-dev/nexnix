/*
    decomp_link.ld - contains linker script for ndecomp
    Copyright 2022 - 2024 The NexNix Project

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

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(NbDecompMain)

NEXBOOT_LOADADDR = 0x50000;

PHDRS
{
    ndecomp PT_LOAD;
}

SECTIONS
{
    . = NEXBOOT_LOADADDR;
    .text : {
        *(.text*)
    } :ndecomp

    .data : {
        *(.data*)
    } :ndecomp

    .rodata : {
        *(.rodata*)
    } :ndecomp

    .bss : {
        *(.bss*)
    } :ndecomp

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.eh_frame*)
        *(.comment)
        *(.debug*)
        *(.symtab)
        *(.strtab)
        *(.rel*)
        *(.rela*)
    }
}

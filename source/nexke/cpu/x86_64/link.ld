/*
    link.ld - contains linker script for nexke
    Copyright 2023 The NexNix Project

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
ENTRY(NkMain)

PHDRS
{
    nexkeText PT_LOAD;
    nexkeData PT_LOAD;
}

SECTIONS
{
    . = 0xFFFFFFFF80000000;
    .text : {
        *(.text*)
    } :nexkeText

    . = ALIGN(4096);

    .rodata : {
        *(.rodata*)
    } :nexkeText

    . = ALIGN(4096);

    .data : {
        *(.data*)
    } :nexkeData

    . = ALIGN(4096);

    .bss : {
        *(.bss*)
    } :nexkeData

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.comment)
        *(.symtab)
        *(.strtab)
        *(.rel*)
        *(.rela*)
    }
}

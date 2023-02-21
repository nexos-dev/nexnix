/*
    bootrecLinkStage2.ld - contains linker script for boot records
    Copyright 2022 The NexNix Project

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

ENTRY(start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x7C00;
    .text : AT(0x0) {
        *(.text)
    }
    /* lld oddly includes some sections like the below in freestanding mode. Discard them */
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
        *(.gnu*)
        *(.note*)
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.eh_frame*)
        *(.data)
        *(.bss)
        *(.debug*)
        *(.symtab)
        *(.strtab)
        *(.rel*)
        *(.rela*)
    }
}

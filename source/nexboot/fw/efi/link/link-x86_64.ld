/*
    link.ld - contains linker script for nexboot
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

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(NbEfiEntry)

SECTIONS
{
    .text 0x1000 : {
        *(.text*)
    }
    . = ALIGN(4096);
    .data : {
        *(.data*)
    }
    . = ALIGN(4096);
    .rodata : {
        *(.rodata*)
    }
    . = ALIGN(4096);
    .bss : {
        *(.bss*)
    }
    . = ALIGN(4096);

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.dynamic)
        *(.eh_frame)
        *(.debug*)
        *(.rela.eh_frame)
    }
}

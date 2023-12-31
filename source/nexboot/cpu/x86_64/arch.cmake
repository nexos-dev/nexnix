#[[
    arch.cmake - contains x86_64 CMake stuff
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
]]

# Option for PAE
nexnix_add_option(NEXNIX_X86_64_LA57 "Specifies if LA57 is supported" ON)

# CPU related sources
list(APPEND NEXBOOT_SOURCES
    cpu/x86_64/cpu.c
    cpu/x86_64/cpu.asm
    cpu/x86_64/as.c)

list(APPEND NEXBOOT_CPU_HEADERS include/nexboot/cpu/x86_64/cpu.h)

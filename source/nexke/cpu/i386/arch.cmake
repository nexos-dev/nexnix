#[[
    arch.cmake - contains build system for nexke i386
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
]]

# Option for PAE
nexnix_add_option(NEXNIX_I386_PAE "Specifies if PAE should be used" ON)
add_compile_options(-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)

# Arch header
set(NEXKE_ARCH_HEADER "${CMAKE_SOURCE_DIR}/include/nexke/cpu/i386/i386.h")

# Set linker script
set(NEXKE_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cpu/i386/link.ld")

# CPU sources
list(APPEND NEXKE_SOURCES
    cpu/i386/cpudep.c
    cpu/i386/cpuhelp.c
    cpu/i386/cpu.asm
    cpu/i386/trap.asm
    cpu/x86/cpuid.c
    cpu/x86/exec.c
    mm/ptab.c)

if(NEXNIX_I386_PAE)
    list(APPEND NEXKE_SOURCES cpu/i386/mulpae.c)
else()
    list(APPEND NEXKE_SOURCES cpu/i386/mul.c)
endif()

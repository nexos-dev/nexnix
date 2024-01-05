#[[
    overrides-gnu.cmake - contains CMake variable overrides
    Copyright 2021 - 2024 The NexNix Project

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

if(NOT "${__flags_initialized}" STREQUAL "1")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -nostdlib -ffreestanding")
    set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -ffreestanding -fno-stack-protector")
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -ffreestanding -fno-stack-protector")

    if("${NEXNIX_BASEARCH}" STREQUAL "x86" OR "${NEXNIX_BASEARCH}" STREQUAL "arm")
        set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mgeneral-regs-only")
    elseif("${NEXNIX_ARCH}" STREQUAL "riscv64")
        set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mno-relax")
        set(CMAKE_ASM_FLAGS_INIT "${CMAKE_ASM_FLAGS_INIT}")
    elseif("${NEXNIX_ARCH}" STREQUAL "riscv32")
        set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mabi=ilp32 -march=rv32ima")
        set(CMAKE_ASM_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mabi=ilp32 -march=rv32ima")
    endif()

    set(CMAKE_ASM_FLAGS_INIT "${CMAKE_ASM_FLAGS_INIT} -ffreestanding")
    set(CMAKE_C_STANDARD 99)

    if("${NEXNIX_ARCH}" STREQUAL "x86_64")
        set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -mno-red-zone -mcmodel=large")
        set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -mcmodel=large")
        set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
        set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -mno-red-zone")
    endif()

    set(CMAKE_SHARED_LINKER_FLAGS_INIT ${CMAKE_EXE_LINKER_FLAGS_INIT})
endif()

set(__flags_initialized 1)

#[[
    arch.cmake - contains build system for nexke rv64
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

# Arch header
set(NEXKE_ARCH_HEADER "${CMAKE_SOURCE_DIR}/include/nexke/cpu/riscv64/riscv64.h")

# Set linker script
set(NEXKE_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/cpu/riscv64/link.ld")

list(APPEND NEXKE_SOURCES "cpu/riscv64/cpudep.c")

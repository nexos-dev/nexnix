#[[
    CMakeLists.txt - contains build system for nnelf2efi
    Copyright 2022 The NexNix Project

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

cmake_minimum_required(VERSION 3.00)
project(nnelf2efi VERSION 0.0.1)
enable_language(C)

# Add sources
list(APPEND NNELF2EFI_SOURCES main.c)

# The ELF structures are quite messy.
# ELF32 and ELF64 have different name sfor each structure
# To prevent nnelf2efi from becoming too complicated, we
# compile two version: one named "nnelf2efi32", and one named "nnelf2efi64"
# They share a common codebase, with the only difference being that the 32-bit
# version is compiled with -DIS_ELF32, and 64-bit one with -DIS_ELF64

# Create programs
add_executable(nnelf2efi32 ${NNELF2EFI_SOURCES})
target_compile_definitions(nnelf2efi32 PRIVATE IS_ELF32)
target_link_libraries(nnelf2efi32 PUBLIC LibNex::nex)

add_executable(nnelf2efi64 ${NNELF2EFI_SOURCES})
target_compile_definitions(nnelf2efi64 PRIVATE IS_ELF64)
target_link_libraries(nnelf2efi64 PUBLIC LibNex::nex)

# Install it
install(TARGETS nnelf2efi32)
install(TARGETS nnelf2efi64)

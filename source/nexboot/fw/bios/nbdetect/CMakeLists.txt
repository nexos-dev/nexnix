#[[
    CMakeLists.txt - contains build system for BIOS nbdetect
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

add_executable(nbdetect nbdetect.S)
# Ensure code knows toolchain being used
if(${NEXNIX_TOOLCHAIN} STREQUAL "gnu")
    target_compile_definitions(nbdetect PUBLIC TOOLCHAIN_GNU)
elseif(${NEXNIX_TOOLCHAIN} STREQUAL "llvm")
    target_compile_definitions(nbdetect PUBLIC TOOLCHAIN_LLVM)
endif()

target_link_options(nbdetect PUBLIC -T ${CMAKE_CURRENT_SOURCE_DIR}/link.ld
                    -Wl,--oformat,binary -z notext)
